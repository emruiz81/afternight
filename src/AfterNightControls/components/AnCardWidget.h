// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QFrame>
#include <QString>

class QLabel;
class QLayout;
class QWidget;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnCardWidget : public QFrame
{
public:
    explicit AnCardWidget(const QString& title = QString(), QWidget* parent = nullptr);
    ~AnCardWidget() override = default;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    [[nodiscard]] QLabel* headerLabel() const;
    [[nodiscard]] QWidget* contentWidget() const;
    [[nodiscard]] QLayout* contentLayout() const;
    void setContentLayout(QLayout* layout);

    void refreshTheme();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateHeaderVisibility();

    QLabel* m_headerLabel = nullptr;
    QWidget* m_contentWidget = nullptr;
};

} // namespace an::ui
