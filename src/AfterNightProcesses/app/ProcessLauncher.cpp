// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ProcessLauncher.h"

#include "controls/IAppContext.h"
#include "controls/ImageMaskSelectorControl.h"
#include "controls/ImageViewControl.h"

#include "processes/ProcessCommonUtils.h"

#include <utility>

namespace an::ui
{

namespace
{

[[nodiscard]] IViewRegistry* currentViewRegistry()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getViewRegistry();
    }

    return nullptr;
}

[[nodiscard]] IDialogService* currentDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getDialogService();
    }

    return nullptr;
}

[[nodiscard]] QWidget* currentLaunchParent()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getMainWindow();
    }

    return nullptr;
}

} // anonymous namespace

ProcessLauncher::ProcessLauncher(std::string descriptorId) : m_descriptorId(std::move(descriptorId)) {}

const std::string& ProcessLauncher::descriptorId() const noexcept
{
    return m_descriptorId;
}

void ProcessLauncher::launchSingleImageProcess(const std::function<void(ImageViewControl*)>& launch) const
{
    const LaunchNotifications notificationHandlers = notifications();
    const process_registry::ProcessDescriptor* descriptor =
        process_registry::ProcessRegistry::instance().find(m_descriptorId);
    if (descriptor == nullptr)
    {
        LOG_WARNING("A single-image process was asked for through descriptor id '{}', which is not registered",
                    m_descriptorId);
        if (notificationHandlers.notifyError)
        {
            notificationHandlers.notifyError(QStringLiteral("Process"),
                                             QStringLiteral("This process is not registered and cannot be opened."));
        }
        return;
    }

    SingleImageLaunchState state;
    auto* viewRegistry = currentViewRegistry();
    state.activeControl = viewRegistry ? viewRegistry->getActiveImageViewControl() : nullptr;
    state.input = process_registry::launchGuardInputFor(*descriptor, state.activeControl);

    (void)an::ui::launchSingleImageProcess(*descriptor, state, notificationHandlers, launch);
}

void ProcessLauncher::launchTopLevelProcess(const std::function<void()>& launch) const
{
    if (launch)
    {
        launch();
    }
}

LaunchNotifications ProcessLauncher::notifications() const
{
    LaunchNotifications handlers;

    handlers.notifyError = [](const QString& title, const QString& message) {
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyErrorMessage(currentLaunchParent(), title, message);
        }
    };

    handlers.notifyWarning = [](const QString& title, const QString& message) {
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyWarnMessage(currentLaunchParent(), title, message);
        }
    };

    return handlers;
}

void ProcessLauncher::executeSelectMask() const
{
    auto* control = process_utils::currentActiveViewControl();
    if (!control)
    {
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyErrorMessage(currentLaunchParent(), QStringLiteral("ImageMaskSelector"),
                                              QStringLiteral("No selected image to process"));
        }
        return;
    }

    auto* maskSelector = new ImageMaskSelectorControl(currentLaunchParent(), control);
    maskSelector->show();
}

void ProcessLauncher::executeRemoveMask() const
{
    auto* control = process_utils::currentActiveViewControl();
    if (!control)
    {
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyErrorMessage(currentLaunchParent(), QStringLiteral("Remove Mask"),
                                              QStringLiteral("No selected image to process"));
        }
        return;
    }

    if (control->isMasked())
    {
        LOG_INFO("Removed mask '{}' from image '{}'", control->getCurrentMask()->getUniqueName(),
                 control->getUniqueName());
        control->removeCurrentMask();
        process_utils::updateActiveImageActionsAsync();
    }
}

} // namespace an::ui
