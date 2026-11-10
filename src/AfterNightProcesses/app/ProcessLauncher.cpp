// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ProcessLauncher.h"

#include "controls/IAppContext.h"
#include "controls/ImageMaskSelectorControl.h"
#include "controls/ImageViewControl.h"
#include "controls/TopLevelProcessBaseControl.h"

#include "processes/ProcessCommonUtils.h"

#include <QApplication>
#include <QPointer>

#include <algorithm>
#include <utility>
#include <vector>

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

/// The process windows shown now.
[[nodiscard]] std::vector<QPointer<TopLevelProcessBaseControl>> shownProcessWindows()
{
    std::vector<QPointer<TopLevelProcessBaseControl>> shown;
    for (QWidget* widget : QApplication::allWidgets())
    {
        // A process window declares no meta-object of its own, so it is known by its type.
        auto* window = dynamic_cast<TopLevelProcessBaseControl*>(widget);
        if (window != nullptr && window->isVisible())
        {
            shown.emplace_back(window);
        }
    }
    return shown;
}

/// Runs a launch and states a descriptor's guide page on the window it shows: the process window shown after the
/// launch that was not shown before it. An entry point makes and shows its window itself and hands nothing back, so
/// the window is found by what the launch changed. A launch that shows no window - one a guard of the process's own
/// refused - states nothing.
///
/// @param descriptor the descriptor the launch is for, or null for none; nothing is stated then.
/// @param launch what opens the process.
template <typename Launch>
void launchStatingHelpPath(const process_registry::ProcessDescriptor* descriptor, Launch&& launch)
{
    if (descriptor == nullptr || descriptor->helpPath.empty())
    {
        launch();
        return;
    }

    const QString helpPath = QString::fromStdString(descriptor->helpPath);
    const std::vector<QPointer<TopLevelProcessBaseControl>> before = shownProcessWindows();
    launch();
    for (const QPointer<TopLevelProcessBaseControl>& window : shownProcessWindows())
    {
        if (!window.isNull() && std::find(before.begin(), before.end(), window) == before.end())
        {
            window->setHelpPath(helpPath);
        }
    }
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

    (void)an::ui::launchSingleImageProcess(*descriptor, state, notificationHandlers,
                                           [descriptor, &launch](ImageViewControl* control) {
                                               if (launch)
                                               {
                                                   launchStatingHelpPath(descriptor, [&]() { launch(control); });
                                               }
                                           });
}

void ProcessLauncher::launchTopLevelProcess(const std::function<void()>& launch) const
{
    if (launch)
    {
        // A top-level window reads no guard of its descriptor; the descriptor says which page describes it.
        launchStatingHelpPath(process_registry::ProcessRegistry::instance().find(m_descriptorId), launch);
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
