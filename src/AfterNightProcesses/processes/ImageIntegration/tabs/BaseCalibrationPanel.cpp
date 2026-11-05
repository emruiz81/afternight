// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BaseCalibrationPanel.h"

#include <QObject>

BaseCalibrationPanel::BaseCalibrationPanel(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : an::ui::AnCollapsibleSection(QString(), parent), m_model(model), m_undoStack(undoStack)
{
}

void BaseCalibrationPanel::initializePanelChrome(const QString& objectName, const QString& title)
{
    setObjectName(objectName);
    setTitle(title);
    if (QWidget* cw = contentWidget())
    {
        // Horizontal Expanding so the content fills the panel width.
        // Vertical Preferred so the content only takes its natural height;
        // the parent scroll-area spacer handles remaining space.
        // Using Expanding here would stretch the content and create a gap
        // between the table and the footer buttons inside each panel.
        cw->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    setExpanded(true);
}

void BaseCalibrationPanel::bindContentVisibility()
{
    if (m_content)
    {
        // Visibility toggle handled by the host tab itself now or not needed
    }
}
