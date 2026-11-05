// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>
#include <string>

#include <QIcon>

namespace an::ui::app
{
class AfterNightAppUiElements;
}

class ImageViewControlManager;
class ImageViewControl;
class QObject;

namespace an::ui
{

inline bool shouldDisableImageActions(const bool hasActiveControl, const bool hasImage)
{
    return !hasActiveControl || !hasImage;
}

inline bool shouldEnableMaskRenderingMenu(const bool isMasked)
{
    return isMasked;
}

class AFTERNIGHTCONTROLS_API ActionStateCoordinator
{
public:
    struct Dependencies
    {
        an::ui::app::AfterNightAppUiElements* ui = nullptr;
        ImageViewControlManager* imageViewControlManager = nullptr;
        QObject* timerContext = nullptr;
        bool* isTerminating = nullptr;
        std::function<bool()> isMaximizedViewActive;
        std::function<QIcon(const std::string&)> iconProvider;
    };

    explicit ActionStateCoordinator(const Dependencies& dependencies);

    void setEnableImageActions(bool enable) const;
    void setEnableImageColorActions(bool enable) const;
    void updateRegistryProcessActionStates(ImageViewControl* control) const;
    void updateActiveImageActions() const;
    void updateActiveImageHistory() const;

private:
    Dependencies m_dependencies;
};

} // namespace an::ui
