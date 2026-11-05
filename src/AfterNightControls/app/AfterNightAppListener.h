// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <string>

class ImageViewControl;

namespace an::ui
{
class IAppEventBus;
}

class AFTERNIGHTCONTROLS_API AfterNightAppListener
{
public:
    /// Construct from the abstract event bus interface (used by
    /// controls going through an::ui::appContext()).
    explicit AfterNightAppListener(an::ui::IAppEventBus* eventBus);

    virtual void onViewControlCreated(const ImageViewControl* ctrl) {}

    virtual void onViewControlDestroyed(const ImageViewControl* ctrl) {}

    virtual void onViewControlUpdated(const ImageViewControl* ctrl) {}

    virtual void onActiveViewControlChanged(ImageViewControl* ctrl) {}

    virtual void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                      const ImageViewControl* ctrl)
    {
    }

    // The user opens a process window
    virtual void onProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl) {}

    // A process has been executed
    virtual void onProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl) {}

    virtual void onApplicationClose() {}

    virtual ~AfterNightAppListener();

private:
    an::ui::IAppEventBus* m_eventBus = nullptr;
};
