// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/MaskOverlayCoordinator.h"


#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"

#include <AfterNightPlatform/Logging.h>

namespace an::ui
{

MaskOverlayCoordinator::MaskOverlayCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void MaskOverlayCoordinator::onInvertMaskClicked(const bool checked) const
{
    auto* control = activeControlOrNotify("Invert Mask");
    if (!control)
    {
        return;
    }

    control->setInvertMask(checked);
    if (checked)
    {
        LOG_INFO("Mask invert enabled for image '{}", control->getUniqueName());
    }
    else
    {
        LOG_INFO("Mask invert disabled for image '{}", control->getUniqueName());
    }
}

void MaskOverlayCoordinator::onEnableMaskClicked(const bool checked) const
{
    auto* control = activeControlOrNotify("Enable Mask");
    if (!control)
    {
        return;
    }

    control->setEnableMask(checked);
    if (checked)
    {
        LOG_INFO("Mask enabled for image '{}", control->getUniqueName());
    }
    else
    {
        LOG_INFO("Mask disabled for image '{}", control->getUniqueName());
    }
}

void MaskOverlayCoordinator::onShowMaskClicked(const bool checked) const
{
    auto* control = activeControlOrNotify("Show Mask");
    if (!control)
    {
        return;
    }

    control->setShowMask(checked);
    if (checked)
    {
        LOG_INFO("Show mask enabled for image '{}", control->getUniqueName());
    }
    else
    {
        LOG_INFO("Show mask disabled for image '{}", control->getUniqueName());
    }
}

void MaskOverlayCoordinator::onOverlayRedClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionOverlay_Red"), MaskRenderingMode::red_mode);
}

void MaskOverlayCoordinator::onOverlayGreenClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionOverlay_Green"), MaskRenderingMode::green_mode);
}

void MaskOverlayCoordinator::onOverlayBlueClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionOverlay_Blue"), MaskRenderingMode::blue_mode);
}

void MaskOverlayCoordinator::onOverlayYellowClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionOverlay_Yellow"), MaskRenderingMode::yellow_mode);
}

void MaskOverlayCoordinator::onOverlayCyanClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionOverlay_Cyan"), MaskRenderingMode::cyan_mode);
}

void MaskOverlayCoordinator::onOverlayMagentaClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionOverlay_Magenta"), MaskRenderingMode::magenta_mode);
}

void MaskOverlayCoordinator::onOverlayMultiplyClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionMultiply"), MaskRenderingMode::multiply_mode);
}

void MaskOverlayCoordinator::onOverlayReplaceClicked(const bool checked) const
{
    applyOverlayMode(checked, action("actionReplace"), MaskRenderingMode::replace_mode);
}

std::array<QAction*, kMaskOverlayActionCount> MaskOverlayCoordinator::overlayActions() const
{
    return {
        action("actionOverlay_Red"),    action("actionOverlay_Green"), action("actionOverlay_Blue"),
        action("actionOverlay_Yellow"), action("actionOverlay_Cyan"),  action("actionOverlay_Magenta"),
        action("actionReplace"),        action("actionMultiply"),
    };
}

QAction* MaskOverlayCoordinator::action(const std::string_view objectName) const
{
    return m_dependencies.actions ? m_dependencies.actions(objectName) : nullptr;
}

ImageViewControl* MaskOverlayCoordinator::activeControlOrNotify(const QString& title) const
{
    const QString message = QStringLiteral("No selected image to process");
    if (!m_dependencies.imageViewControlManager)
    {
        return nullptr;
    }

    auto* control = m_dependencies.imageViewControlManager->getActiveImageViewControl();
    if (!canApplyMaskOverlay(control != nullptr))
    {
        if (m_dependencies.notifyErrorMessage)
        {
            m_dependencies.notifyErrorMessage(title, message);
        }
        return nullptr;
    }

    return control;
}

void MaskOverlayCoordinator::applyOverlayMode(const bool checked, QAction* selectedAction,
                                              const MaskRenderingMode mode) const
{
    auto* control = activeControlOrNotify("Set Mask Render mode");
    if (!control)
    {
        return;
    }

    if (selectedAction && shouldForceSelectedOverlayCheckedState(checked))
    {
        selectedAction->setChecked(true);
    }

    for (QAction* action : overlayActions())
    {
        if (!action || action == selectedAction)
        {
            continue;
        }

        action->setEnabled(true);
        action->setChecked(false);
    }

    control->setMaskRenderingMode(mode);
}

} // namespace an::ui
