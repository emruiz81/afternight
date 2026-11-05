// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LogViewerFindTextControl.h"

LogViewerFindTextControl::LogViewerFindTextControl(LogViewerControl* parent)
    : QDialog(parent), m_LogViewerControl(parent)
{
    m_ui = an::ui::controls::buildLogViewerFindTextControlUi(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.closeButton, &QPushButton::released, this, &LogViewerFindTextControl::on_closeButtonPushed);
    QObject::connect(m_ui.clearButton, &QPushButton::released, this, &LogViewerFindTextControl::on_clearButtonPushed);
    QObject::connect(m_ui.lineEdit, &QLineEdit::textChanged, this, &LogViewerFindTextControl::on_lineEditChanged);
    QObject::connect(m_ui.findNextButton, &QPushButton::released, this,
                     &LogViewerFindTextControl::on_findNextButtonPushed);
    QObject::connect(m_ui.regularExpression, &QCheckBox::checkStateChanged, this,
                     &LogViewerFindTextControl::on_regularExpressionClicked);

    setWindowTitle("Search log");

    m_ui.findNextButton->setShortcut(QKeySequence(Qt::Key_Return));

    m_ui.findNextButton->setEnabled(false);
}

void LogViewerFindTextControl::parentDestroyed()
{
    m_LogViewerControl = nullptr;
    close();
}

LogViewerFindTextControl::~LogViewerFindTextControl()
{
    if (m_LogViewerControl)
    {
        m_LogViewerControl->notifyLogViewerFindTextControlClosed();
    }
}

void LogViewerFindTextControl::on_lineEditChanged(const QString& text)
{
    if (text.isEmpty())
    {
        m_ui.findNextButton->setEnabled(false);
    }
    else
    {
        m_ui.findNextButton->setEnabled(true);
    }
}

void LogViewerFindTextControl::on_findNextButtonPushed()
{
    if (m_LogViewerControl)
    {
        m_LogViewerControl->findText(m_ui.lineEdit->text(), m_ui.caseSensitive->isChecked(),
                                     m_ui.matchWholeWord->isChecked(), m_ui.regularExpression->isChecked(),
                                     m_ui.backward->isChecked());
    }
}

void LogViewerFindTextControl::on_clearButtonPushed()
{
    if (m_LogViewerControl)
    {
        m_LogViewerControl->resetSearchCursor();
    }
}

void LogViewerFindTextControl::on_regularExpressionClicked(Qt::CheckState state)
{
    if (state == Qt::Checked)
    {
        m_ui.matchWholeWord->setEnabled(false);
    }
    else
    {
        m_ui.matchWholeWord->setEnabled(true);
    }
}

void LogViewerFindTextControl::on_closeButtonPushed()
{
    this->close();
}
