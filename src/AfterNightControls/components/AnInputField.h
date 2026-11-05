// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnInputField : public QWidget
{
public:
    explicit AnInputField(const QString& labelText = QString(), const QString& placeholderText = QString(),
                          QWidget* parent = nullptr);
    ~AnInputField() override = default;

    void setLabelText(const QString& text);
    [[nodiscard]] QString labelText() const;
    [[nodiscard]] QLabel* label() const;

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    void setPlaceholderText(const QString& text);
    [[nodiscard]] QString placeholderText() const;
    [[nodiscard]] QLineEdit* lineEdit() const;

    void refreshTheme();

private:
    void updateLabelVisibility();

    QLabel* m_label = nullptr;
    QLineEdit* m_lineEdit = nullptr;
};

} // namespace an::ui
