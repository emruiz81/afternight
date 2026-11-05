// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QList>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QPushButton;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnButtonGroup : public QWidget
{
public:
    enum class ButtonVariant
    {
        Secondary,
        Primary
    };

    explicit AnButtonGroup(QWidget* parent = nullptr);
    ~AnButtonGroup() override = default;

    QPushButton* addButton(const QString& text, ButtonVariant variant = ButtonVariant::Secondary);
    void setButtonVariant(QPushButton* button, ButtonVariant variant);
    [[nodiscard]] ButtonVariant buttonVariant(const QPushButton* button) const;

    [[nodiscard]] QList<QPushButton*> buttons() const;
    void clearButtons();

    void refreshTheme();

private:
    static QString variantName(ButtonVariant variant);

    QHBoxLayout* m_layout = nullptr;
};

} // namespace an::ui
