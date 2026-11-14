// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>

class QComboBox;
class QObject;
class QString;

namespace an::ui
{

/// What the classic window's menus and tool bars connect of their own: the image selector, which makes the view it
/// names the active one. The application's actions are connected by the shell core's action bindings.
class AFTERNIGHTCONTROLS_API MenuActionManager
{
public:
    struct Dependencies
    {
        QObject* signalContext = nullptr;
        QComboBox* imageSelector = nullptr;
        std::function<void(const QString&)> onImageSelectorChanged;
    };

    explicit MenuActionManager(const Dependencies& dependencies);

    /// Connects the image selector, the first time it is asked.
    void connectActions() const;

private:
    Dependencies m_dependencies;
    mutable bool m_connected = false;
};

} // namespace an::ui
