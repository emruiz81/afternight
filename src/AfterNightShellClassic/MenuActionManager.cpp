// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "MenuActionManager.h"

#include <QComboBox>
#include <QObject>
#include <QString>

namespace an::ui
{

MenuActionManager::MenuActionManager(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void MenuActionManager::connectActions() const
{
    if (m_connected || !m_dependencies.signalContext)
    {
        return;
    }
    m_connected = true;

    if (m_dependencies.imageSelector && m_dependencies.onImageSelectorChanged)
    {
        QObject::connect(m_dependencies.imageSelector, &QComboBox::currentTextChanged, m_dependencies.signalContext,
                         [this](const QString& text) {
                             if (m_dependencies.onImageSelectorChanged)
                             {
                                 m_dependencies.onImageSelectorChanged(text);
                             }
                         });
    }
}

} // namespace an::ui
