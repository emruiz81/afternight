// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "actions/ActionStateCoordinator.h"

#include "actions/ActionIds.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"

#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QAction>
#include <QTimer>

#include <utility>

namespace an::ui::shell
{

namespace
{

std::string registryDescriptorId(const QAction* action)
{
    if (!action)
    {
        return {};
    }

    const QString descriptorId = action->property("anRegistryDescriptorId").toString().trimmed();
    return descriptorId.toStdString();
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

/// The "Convert to" action of a representation; empty for none.
std::string_view convertActionFor(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::UInt8:
    case an::SampleType::Int8:
        return ids::kConvertTo8Bit;
    case an::SampleType::Int16:
        return ids::kConvertToSigned16Bit;
    case an::SampleType::UInt16:
        return ids::kConvertToUnsigned16Bit;
    case an::SampleType::Int32:
        return ids::kConvertToSigned32Bit;
    case an::SampleType::UInt32:
        return ids::kConvertToUnsigned32Bit;
    case an::SampleType::Float32:
        return ids::kConvertTo32BitFloat;
    case an::SampleType::Float64:
        return ids::kConvertTo64BitFloat;
    }
    return {};
}

} // anonymous namespace

ActionStateCoordinator::ActionStateCoordinator(Dependencies dependencies) : m_dependencies(std::move(dependencies)) {}

void ActionStateCoordinator::setHooks(Hooks hooks)
{
    m_dependencies.setMenuEnabled = std::move(hooks.setMenuEnabled);
    m_dependencies.isTerminating = std::move(hooks.isTerminating);
}

void ActionStateCoordinator::setCustomImageActionEnabled(ImageViewControl* view, const GlobalViewControlActions action,
                                                         const bool enable) const
{
    ImageViewControl* const subject = m_dependencies.subjectView ? m_dependencies.subjectView() : nullptr;
    if (!subject || subject != view)
    {
        return;
    }

    switch (action)
    {
    case GlobalViewControlActions::STATISTICS_ACTION:
        setEnabled(ids::kStatistics, enable);
        break;
    case GlobalViewControlActions::HISTOGRAM_ACTION:
        setEnabled(ids::kHistogram, enable);
        break;
    case GlobalViewControlActions::FITS_HEADER_ACTION:
        setEnabled(ids::kFITSHeader, enable);
        break;
    case GlobalViewControlActions::EXIF_ACTION:
        setEnabled(ids::kImageEXIFViewer, enable);
        break;
    case GlobalViewControlActions::SUMMARY_ACTION:
        setEnabled(ids::kSummary, enable);
        break;
    }
}

QAction* ActionStateCoordinator::action(const std::string_view id) const
{
    return m_dependencies.actions ? m_dependencies.actions(id) : nullptr;
}

void ActionStateCoordinator::setEnabled(const std::string_view id, const bool enabled) const
{
    if (QAction* const found = action(id))
    {
        found->setEnabled(enabled);
    }
}

void ActionStateCoordinator::setChecked(const std::string_view id, const bool checked) const
{
    if (QAction* const found = action(id))
    {
        found->setChecked(checked);
    }
}

void ActionStateCoordinator::setMenuEnabled(const std::string_view menuId, const bool enabled) const
{
    if (m_dependencies.setMenuEnabled)
    {
        m_dependencies.setMenuEnabled(menuId, enabled);
    }
}

void ActionStateCoordinator::setEnableImageActions(const bool enable) const
{
    setEnabled(ids::kSaveAs, enable);
    setEnabled(ids::kClose, enable);
    setEnabled(ids::kCloseAll, enable);
    setEnabled(ids::kCloseAllOthers, enable);

    setMenuEnabled("menuActions", enable);
    setMenuEnabled("menuView", enable);

    setMenuEnabled("menuMask", enable);

    setMenuEnabled("menuConvert_Format", enable);
    setEnabled(ids::kConvertTo8Bit, enable);
    setEnabled(ids::kConvertToSigned16Bit, enable);
    setEnabled(ids::kConvertToUnsigned16Bit, enable);
    setEnabled(ids::kConvertToSigned32Bit, enable);
    setEnabled(ids::kConvertToUnsigned32Bit, enable);
    setEnabled(ids::kConvertTo32BitFloat, enable);
    setEnabled(ids::kConvertTo64BitFloat, enable);
    setMenuEnabled("menuGeometry", enable);
    setEnabled(ids::kHorizontalMirror, enable);
    setEnabled(ids::kVerticalMirror, enable);

    setMenuEnabled("menuInformation", enable);
    setMenuEnabled("menuAdjustments", enable);
    setMenuEnabled("menuIntensity_Transformations", enable);
    setMenuEnabled("menuStar_Processing", enable);
    setMenuEnabled("menuNoise_Reduction", enable);
    setMenuEnabled("menuColor_Spaces", true);
    setMenuEnabled("menuGeometry_2", enable);
    setMenuEnabled("menuBackground_Modelization", enable);
    setMenuEnabled("menuDetails", enable);
    setMenuEnabled("menuFilters", enable);
    setMenuEnabled("menuMorphology", enable);

    setMenuEnabled("menuColor_Calibration", enable);

    setEnabled(ids::kStatistics, enable);
    setEnabled(ids::kHistogram, enable);

    setMenuEnabled("menuMask_Generation", enable);
    setMenuEnabled("menuSequence", enable);

    setMenuEnabled("menuPixel_Math", enable);

    setEnabled(ids::kImageBlend, enable);
    setEnabled(ids::kImageCompare, enable);
    setEnabled(ids::kNoiseGenerator, enable);
    setEnabled(ids::kImageListAnalyzer, true);

    setMenuEnabled("menuMultiscale", enable);

    setEnabled(ids::kFitImagesToWindow, enable);

    setEnabled(ids::kGroupAllImages, enable);
    setEnabled(ids::kSplitAllImages, enable);

    if (!enable)
    {
        setEnabled(ids::kFITSHeader, enable);
        setEnabled(ids::kAstrometryInfo, enable);
        setEnabled(ids::kImageEXIFViewer, enable);
        setEnabled(ids::kPlateSolve, enable);
        setEnabled(ids::kAnnotateImage, enable);
        setEnabled(ids::kSummary, enable);
        setEnabled(ids::kShowInFileBrowser, enable);
    }

    setEnableImageColorActions(enable);
}

void ActionStateCoordinator::setEnableImageColorActions(const bool enable) const
{
    setMenuEnabled("menuExtract_Channel", enable);

    setEnabled(ids::kAdjustHSL, enable);
    setEnabled(ids::kAdjustLightnessChromaHue, enable);
    setEnabled(ids::kConvertToRGB, !enable);
    setEnabled(ids::kConvertToGrayscale, enable);

    setEnabled(ids::kColorCalibration2, enable);
    setEnabled(ids::kPhotometricColorCalibration, enable);
    setEnabled(ids::kSpectrophotometricColorCalibration, enable);
    setEnabled(ids::kBackgroundNeutralization, enable);

    setEnabled(ids::kColorSaturation, enable);
    setEnabled(ids::kSCNR, enable);

    setEnabled(ids::kLRGBCombination, enable);
    setEnabled(ids::kNBRGBCombination, enable);
    setEnabled(ids::kNarrowbandPaletteComposer, true);
}

void ActionStateCoordinator::updateRegistryProcessActionStates(ImageViewControl* control) const
{
    if (!m_dependencies.registryActions)
    {
        return;
    }

    for (QAction* action : m_dependencies.registryActions())
    {
        const std::string descriptorId = registryDescriptorId(action);
        if (descriptorId.empty())
        {
            continue;
        }
        const auto* descriptor = process_registry::ProcessRegistry::instance().find(descriptorId);
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
    auto* control = m_dependencies.subjectView ? m_dependencies.subjectView() : nullptr;
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
        setEnabled(ids::kConvertToSigned16Bit, false);
        setEnabled(ids::kConvertToSigned32Bit, false);
        setEnabled(ids::kConvertToUnsigned32Bit, false);
        setEnabled(ids::kConvertTo32BitFloat, false);
        setEnabled(ids::kConvertTo64BitFloat, false);
    }

    if (control->isSequence())
    {
        setMenuEnabled("menuSequence", true);
        setEnabled(ids::kConvertToSER, true);
        setEnabled(ids::kConvertToImageList, true);
        setEnabled(ids::kExport, true);
        setEnabled(ids::kConvertToFITS, false);

        if (control->isSERSequence())
        {
            setEnabled(ids::kConvertToSER, false);
        }
        else if (control->isImageListSequence())
        {
            setEnabled(ids::kConvertToImageList, false);
        }
    }
    else
    {
        setMenuEnabled("menuSequence", false);
    }

    // The action that would convert to the representation the image already has is the one disabled.
    if (const std::string_view current = convertActionFor(descriptor->sampleType()); !current.empty())
    {
        setEnabled(current, false);
    }

    setEnabled(ids::kStatistics, control->isStatisticsActionEnabled());
    setEnabled(ids::kHistogram, control->isHistogramActionEnabled());

    setEnabled(ids::kFITSHeader, control->isFitsHeaderActionEnabled());
    setEnabled(ids::kAstrometryInfo, true);
    setEnabled(ids::kImageEXIFViewer, control->isExifActionEnabled());
    setEnabled(ids::kPlateSolve, control->canPlateSolveCurrentImage());
    setEnabled(ids::kAnnotateImage, control->canAnnotateCurrentImage());
    setEnabled(ids::kSummary, true);
    // Offered while the image is a file: read from one, saved to one, or a sequence's.
    setEnabled(ids::kShowInFileBrowser, !control->getFileName().empty());

    QTimer::singleShot(0, m_dependencies.timerContext, [this]() {
        const bool terminating = m_dependencies.isTerminating ? m_dependencies.isTerminating() : false;
        if (terminating)
        {
            return;
        }

        // Maximize View acts on the view a person looks through.
        auto* looking = m_dependencies.lookingView ? m_dependencies.lookingView() : nullptr;
        QAction* const maximize = action(ids::kMaximizeView);
        if (!looking || !maximize)
        {
            return;
        }

        if (looking->isMaximized())
        {
            maximize->setIcon(m_dependencies.iconProvider ? m_dependencies.iconProvider("fullscreen_exit") : QIcon());
            maximize->setText("Restore View");
        }
        else
        {
            maximize->setIcon(m_dependencies.iconProvider ? m_dependencies.iconProvider("fullscreen") : QIcon());
            maximize->setText("Maximize View");
        }

        maximize->setEnabled(!looking->isInFloatingWindow());
    });

    if (m_dependencies.isMaximizedViewActive && m_dependencies.isMaximizedViewActive())
    {
        setEnabled(ids::kSplitAllImages, false);
        setEnabled(ids::kGroupAllImages, false);
    }
    else
    {
        setEnabled(ids::kSplitAllImages, true);
        setEnabled(ids::kGroupAllImages, true);
    }

    setEnabled(ids::kSelectMask, true);

    if (shouldEnableMaskRenderingMenu(control->isMasked()))
    {
        const MaskRenderingMode mode = control->getMaskRenderingMode();
        setEnabled(ids::kRemoveMask, true);
        setEnabled(ids::kInvertMask, true);
        setChecked(ids::kInvertMask, control->isMaskInverted());
        setEnabled(ids::kEnableMask, true);
        setChecked(ids::kEnableMask, control->isMaskEnabled());
        setEnabled(ids::kShowMask, true);
        setChecked(ids::kShowMask, control->isShowMaskEnabled());
        setMenuEnabled("menuRendering_Mode", true);
        setChecked(ids::kOverlayRed, mode == MaskRenderingMode::red_mode);
        setChecked(ids::kOverlayGreen, mode == MaskRenderingMode::green_mode);
        setChecked(ids::kOverlayBlue, mode == MaskRenderingMode::blue_mode);
        setChecked(ids::kOverlayYellow, mode == MaskRenderingMode::yellow_mode);
        setChecked(ids::kOverlayCyan, mode == MaskRenderingMode::cyan_mode);
        setChecked(ids::kOverlayMagenta, mode == MaskRenderingMode::magenta_mode);
        setChecked(ids::kReplace, mode == MaskRenderingMode::replace_mode);
        setChecked(ids::kMultiply, mode == MaskRenderingMode::multiply_mode);
    }
    else
    {
        setEnabled(ids::kRemoveMask, false);
        setChecked(ids::kInvertMask, false);
        setEnabled(ids::kInvertMask, false);
        setChecked(ids::kEnableMask, false);
        setEnabled(ids::kEnableMask, false);
        setChecked(ids::kShowMask, false);
        setEnabled(ids::kShowMask, false);
        setMenuEnabled("menuRendering_Mode", false);
    }

    updateRegistryProcessActionStates(control);
}

void ActionStateCoordinator::updateActiveImageHistory() const
{
    QAction* const undo = action(ids::kUndo);
    QAction* const redo = action(ids::kRedo);
    QAction* const history = action(ids::kImageHistory);
    if (!undo || !redo || !history)
    {
        return;
    }

    auto* control = m_dependencies.subjectView ? m_dependencies.subjectView() : nullptr;
    if (!control || !control->viewHistory())
    {
        undo->setEnabled(false);
        redo->setEnabled(false);
        history->setEnabled(false);
        return;
    }

    const an::ui::IViewHistory* viewHistory = control->viewHistory();
    undo->setText("Undo");
    history->setEnabled(true);

    if (viewHistory->hasUndo())
    {
        undo->setEnabled(true);
        const auto name = viewHistory->previousStepName();
        if (!name.empty())
        {
            const std::string undoLabel = fmt::format("Undo <{}>", name);
            undo->setText(QString::fromUtf8(undoLabel));
        }
    }
    else
    {
        undo->setEnabled(false);
    }

    redo->setText("Redo");
    if (viewHistory->hasRedo())
    {
        redo->setEnabled(true);
        const auto name = viewHistory->nextStepName();
        if (!name.empty())
        {
            const std::string redoLabel = fmt::format("Redo <{}>", name);
            redo->setText(QString::fromUtf8(redoLabel));
        }
    }
    else
    {
        redo->setEnabled(false);
    }
}

} // namespace an::ui::shell
