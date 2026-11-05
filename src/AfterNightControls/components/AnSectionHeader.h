// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnSectionHeader : public QWidget
{
public:
    explicit AnSectionHeader(const QString& title = QString(), const QString& description = QString(),
                             const QString& actionText = QString(), QWidget* parent = nullptr);
    ~AnSectionHeader() override = default;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;
    [[nodiscard]] QLabel* titleLabel() const;

    void setDescription(const QString& description);
    [[nodiscard]] QString description() const;
    [[nodiscard]] QLabel* descriptionLabel() const;

    void setActionText(const QString& actionText);
    [[nodiscard]] QString actionText() const;
    [[nodiscard]] QPushButton* actionButton() const;

    void refreshTheme();

private:
    void updateDescriptionVisibility();
    void updateActionVisibility();

    QLabel* m_titleLabel = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QPushButton* m_actionButton = nullptr;
};

} // namespace an::ui
