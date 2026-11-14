// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application's message boxes as the theme draws them, and the window flags a modal one sets aside while it is up.

#include "AfterNightControlsExport.h"

#include <QMap>
#include <QMessageBox>
#include <QString>
#include <Qt>

class QWidget;

namespace an::ui
{

/// Takes a flag - by default "stays on top" - off every visible top-level window while it lives, and puts it back as
/// it goes, so a modal dialog opened meanwhile is not hidden behind a window that keeps itself on top.
class AFTERNIGHTCONTROLS_API ScopedTopLevelFlagRestorer
{
public:
    explicit ScopedTopLevelFlagRestorer(Qt::WindowFlags clearFlag = Qt::WindowStaysOnTopHint);
    ~ScopedTopLevelFlagRestorer();

    ScopedTopLevelFlagRestorer(const ScopedTopLevelFlagRestorer&) = delete;
    ScopedTopLevelFlagRestorer& operator=(const ScopedTopLevelFlagRestorer&) = delete;

private:
    QMap<QWidget*, Qt::WindowFlags> m_originalFlags;
    Qt::WindowFlags m_clearFlag;
};

/// Gives the buttons of a message box the roles the theme draws them by: the accepting one primary, the declining ones
/// secondary, Reset neutral, the destructive ones a danger.
AFTERNIGHTCONTROLS_API void applyModernMessageBoxButtonRoles(QMessageBox& messageBox);

/// Shows a message box over `parent` - modal to its window, or to the application when it has none - with selectable
/// text and themed buttons, and returns the button that closed it.
AFTERNIGHTCONTROLS_API QMessageBox::StandardButton
showModernMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                     QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                     QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

} // namespace an::ui
