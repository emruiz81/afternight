// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "LogViewerFindTextControlUiBuilder.h"
#include "LogViewerControl.h"

class LogViewerFindTextControl : public QDialog
{
public:
    LogViewerFindTextControl(LogViewerControl* parent);

    void parentDestroyed();

    ~LogViewerFindTextControl();

private:
    void on_closeButtonPushed();

    void on_lineEditChanged(const QString& text);

    void on_findNextButtonPushed();

    void on_clearButtonPushed();

    void on_regularExpressionClicked(Qt::CheckState state);

private:
    LogViewerControl* m_LogViewerControl = nullptr;

    an::ui::controls::LogViewerFindTextControlUiElements m_ui;
};
