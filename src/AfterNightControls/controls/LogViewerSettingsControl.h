// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "LogViewerSettingsControlUiBuilder.h"

#include "LogViewerControl.h"

class LogViewerSettingsControl : public QDialog
{
public:
    LogViewerSettingsControl(LogViewerControl* parent);

    void parentDestroyed();

    ~LogViewerSettingsControl();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_fontColorPickerButtonPushed();
    void on_backgroundColorPickerButtonPushed();

    void on_fontHighlightColorPickerButtonPushed();
    void on_backgroundHighlightColorPickerButtonPushed();

    void on_defaultFontSizeValueChanged(int value);

    void on_fontChanged(const QFont& font);

    void on_boldClicked(Qt::CheckState state);
    void on_italicClicked(Qt::CheckState state);

private:
    void setFontColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setFontHighlightColor(const QColor& color);
    void setBackgroundHighlightColor(const QColor& color);

    void applyChanges();

    void updateSettings();

    LogViewerControl* m_LogViewerControl = nullptr;

    an::ui::controls::LogViewerSettingsControlUiElements m_ui;
};
