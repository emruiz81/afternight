// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Finding and answering the dialogs the integration's windows open without a nested event loop. A window opens a
/// message box, a question or an input dialog with `open()` and continues in a callback, so a case looks for the open
/// dialog after the action that opened it, answers it, and pumps the event loop before it asserts.

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QString>
#include <QWidget>

#include <vector>

namespace an::tests::integration
{

/// Runs the event loop's pending work, deferred deletions included, so a dialog's callback has run and a dialog that
/// closed is gone.
inline void pumpDialogEvents()
{
    for (int i = 0; i < 3; ++i)
    {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/// Every dialog of type `DialogT` that is open now, in the order the application lists its windows.
template <typename DialogT> [[nodiscard]] std::vector<DialogT*> openDialogsOf()
{
    std::vector<DialogT*> dialogs;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        auto* dialog = dynamic_cast<DialogT*>(widget);
        if (dialog != nullptr && dialog->isVisible())
        {
            dialogs.push_back(dialog);
        }
    }
    return dialogs;
}

/// The open message box whose title contains `title` (every open one when `title` is empty), or null.
[[nodiscard]] inline QMessageBox* findOpenMessageBox(const QString& title = {})
{
    for (QMessageBox* box : openDialogsOf<QMessageBox>())
    {
        if (title.isEmpty() || box->windowTitle().contains(title))
        {
            return box;
        }
    }
    return nullptr;
}

/// Answers the open message box whose title contains `title` with `button`, and pumps the event loop so its callback
/// runs. Returns false when no such box is open or it has no such button.
inline bool answerOpenMessageBox(const QString& title, QMessageBox::StandardButton button)
{
    QMessageBox* box = findOpenMessageBox(title);
    if (box == nullptr)
    {
        return false;
    }
    QAbstractButton* pressed = box->button(button);
    if (pressed == nullptr)
    {
        return false;
    }
    pressed->click();
    pumpDialogEvents();
    return true;
}

/// Answers the open input dialog whose title contains `title` with `text` and OK, and pumps the event loop. Returns
/// false when no such dialog is open.
inline bool answerOpenTextInput(const QString& title, const QString& text)
{
    for (QInputDialog* dialog : openDialogsOf<QInputDialog>())
    {
        if (title.isEmpty() || dialog->windowTitle().contains(title))
        {
            dialog->setTextValue(text);
            dialog->accept();
            pumpDialogEvents();
            return true;
        }
    }
    return false;
}

/// Rejects every dialog still open, so none outlives the case that opened it, and pumps the event loop.
inline void closeOpenDialogs()
{
    for (QDialog* dialog : openDialogsOf<QDialog>())
    {
        dialog->reject();
    }
    pumpDialogEvents();
}

} // namespace an::tests::integration
