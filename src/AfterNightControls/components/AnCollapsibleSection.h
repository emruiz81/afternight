// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QString>
#include <QWidget>

class QLayout;
class QPropertyAnimation;
class QToolButton;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnCollapsibleSection : public QWidget
{
public:
    explicit AnCollapsibleSection(const QString& title = QString(), QWidget* parent = nullptr);
    ~AnCollapsibleSection() override = default;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    void setContentLayout(QLayout* layout);
    [[nodiscard]] QLayout* contentLayout() const;
    [[nodiscard]] QWidget* contentWidget() const;

    void setExpanded(bool expanded);
    [[nodiscard]] bool isExpanded() const;

    void setAnimationDuration(int milliseconds);
    [[nodiscard]] int animationDuration() const;

    [[nodiscard]] QToolButton* toggleButton() const;

    void refreshTheme();

protected:
    void changeEvent(QEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] int contentHeightHint() const;
    [[nodiscard]] int contentWidthHint() const;
    void applyExpandedState(bool expanded);
    void queueExpandedContentHeightSync();
    void syncExpandedContentHeight();

    QToolButton* m_toggleButton = nullptr;
    QWidget* m_contentWidget = nullptr;
    QPropertyAnimation* m_contentAnimation = nullptr;
    bool m_isExpanded = false;
    bool m_contentHeightSyncQueued = false;
    QColor m_bgColor;
    QColor m_borderColor;
    int m_radius = 8;
};

} // namespace an::ui
