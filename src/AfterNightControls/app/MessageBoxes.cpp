// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/MessageBoxes.h"

#include <QAbstractButton>
#include <QApplication>
#include <QWidget>

namespace an::ui
{

namespace
{

void applyMessageBoxButtonRole(QMessageBox& messageBox, const QMessageBox::StandardButton standardButton,
                               const char* role)
{
    if (!role)
    {
        return;
    }

    if (auto* button = messageBox.button(standardButton))
    {
        button->setProperty("anProcessButtonRole", QString::fromUtf8(role));
    }
}

} // namespace

ScopedTopLevelFlagRestorer::ScopedTopLevelFlagRestorer(const Qt::WindowFlags clearFlag) : m_clearFlag(clearFlag)
{
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevels)
    {
        if (!widget || !widget->isVisible())
        {
            continue;
        }

        const Qt::WindowFlags flags = widget->windowFlags();
        if (flags & clearFlag)
        {
            m_originalFlags[widget] = flags;
            widget->setWindowFlags(flags & ~clearFlag);
            widget->hide();
            widget->show();
        }
    }
}

ScopedTopLevelFlagRestorer::~ScopedTopLevelFlagRestorer()
{
    for (auto it = m_originalFlags.begin(); it != m_originalFlags.end(); ++it)
    {
        QWidget* widget = it.key();
        const Qt::WindowFlags flags = it.value();
        if (widget)
        {
            widget->setWindowFlags(flags);
            widget->hide();
            widget->show();
        }
    }
}

void applyModernMessageBoxButtonRoles(QMessageBox& messageBox)
{
    applyMessageBoxButtonRole(messageBox, QMessageBox::Ok, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Yes, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::YesToAll, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Save, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::SaveAll, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Open, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Apply, "primary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Retry, "primary");

    applyMessageBoxButtonRole(messageBox, QMessageBox::No, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::NoToAll, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Cancel, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Close, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Ignore, "secondary");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Help, "secondary");

    applyMessageBoxButtonRole(messageBox, QMessageBox::Reset, "neutral");

    applyMessageBoxButtonRole(messageBox, QMessageBox::Discard, "danger");
    applyMessageBoxButtonRole(messageBox, QMessageBox::Abort, "danger");
}

QMessageBox::StandardButton showModernMessageBox(QWidget* parent, const QMessageBox::Icon icon, const QString& title,
                                                 const QString& text, const QMessageBox::StandardButtons buttons,
                                                 const QMessageBox::StandardButton defaultButton)
{
    QMessageBox messageBox(parent);
    messageBox.setObjectName(QStringLiteral("anMessageBox"));
    messageBox.setProperty("anDialogVariant", QStringLiteral("messageBox"));
    messageBox.setIcon(icon);
    messageBox.setWindowTitle(title);
    messageBox.setText(text);
    messageBox.setStandardButtons(buttons);
    messageBox.setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    messageBox.setTextInteractionFlags(Qt::TextSelectableByMouse);

    if (defaultButton != QMessageBox::NoButton)
    {
        messageBox.setDefaultButton(defaultButton);
    }

    applyModernMessageBoxButtonRoles(messageBox);

    return static_cast<QMessageBox::StandardButton>(messageBox.exec());
}

} // namespace an::ui
