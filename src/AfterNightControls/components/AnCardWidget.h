// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QFrame>
#include <QString>

class QLabel;
class QLayout;
class QVariantAnimation;
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
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateHeaderVisibility();
    void startHoverAnimation(const QColor& targetBg);

    QLabel* m_headerLabel = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVariantAnimation* m_hoverAnimation = nullptr;
    QColor m_currentBgColor;
    QColor m_currentBorderColor;
};

} // namespace an::ui
