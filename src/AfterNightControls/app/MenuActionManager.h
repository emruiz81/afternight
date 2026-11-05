// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <cstddef>
#include <functional>
#include <string_view>

#include "app/IconManager.h"

class QAction;
class QObject;
class QComboBox;

namespace an::ui
{
class FileManager;
class ImageOperationsCoordinator;
class MaskOverlayCoordinator;
} // namespace an::ui

namespace an::ui::app
{
class AfterNightAppUiElements;
}

namespace an::ui
{

inline size_t countConnectableActions(const std::initializer_list<QAction*>& actions)
{
    size_t count = 0;
    for (QAction* action : actions)
    {
        if (action != nullptr)
        {
            ++count;
        }
    }
    return count;
}

inline bool shouldConnectMenuActions(const bool alreadyConnected)
{
    return !alreadyConnected;
}

class AFTERNIGHTCONTROLS_API MenuActionManager
{
public:
    struct Dependencies
    {
        QObject* signalContext = nullptr;
        an::ui::app::AfterNightAppUiElements* ui = nullptr;
        QComboBox* imageSelector = nullptr;
        std::function<FileManager*()> fileManager;
        std::function<ImageOperationsCoordinator*()> imageOperations;
        std::function<MaskOverlayCoordinator*()> maskOverlay;
        std::function<bool(const std::string&)> tryLaunchRegistryProcessById;
        std::function<void(const QString&)> onImageSelectorChanged;
        std::function<void()> onNewSequenceRequested;
        std::function<void()> onLogViewerRequested;
        std::function<void()> onOpenHelpRequested;
        std::function<void()> onAboutRequested;
        std::function<void()> onSystemInformationRequested;
        std::function<void()> onOpenSettingsRequested;
        std::function<void()> onOpenExtensionManagerRequested;
        std::function<void()> onLockLayoutRequested;
        std::function<void()> onUnlockLayoutRequested;
        std::function<void()> onGroupAllImagesRequested;
        std::function<void()> onSplitAllImagesRequested;
        std::function<IconManagerSptr()> iconManager;
    };

    explicit MenuActionManager(const Dependencies& dependencies);

    void markRegistryActionPending(QAction* action) const;
    void bindRegistryAction(QAction* action, std::string_view category, std::string_view descriptor_id = {}) const;
    void connectActions() const;
    void connectProcessActions() const;
    void setActionIcons() const;

private:
    void connectNonProcessActions() const;

    Dependencies m_dependencies;
    QObject* m_signalContext = nullptr;
    an::ui::app::AfterNightAppUiElements* m_ui = nullptr;
    QComboBox* m_imageSelector = nullptr;
    mutable bool m_nonProcessActionsConnected = false;
    mutable bool m_processActionsConnected = false;
};

} // namespace an::ui
