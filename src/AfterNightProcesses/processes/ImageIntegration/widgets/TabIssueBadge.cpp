// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TabIssueBadge.h"

#include "theme/AnDesignTokens.h"

#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QTabBar>

#include <algorithm>

TabIssueBadge::TabIssueBadge(QWidget* parent) : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    refreshTheme();
}

void TabIssueBadge::setStatus(const Status status)
{
    if (m_status == status)
    {
        return;
    }

    m_status = status;
    refreshTheme();
}

void TabIssueBadge::refreshTheme()
{
    const auto& tokenSet = an::ui::tokens();
    const auto& colors = tokenSet.colors;
    const bool darkTheme = an::ui::isDarkTheme();

    const QColor tone = (m_status == Status::Error) ? colors.statusError : colors.statusWarning;
    m_fillColor = tone;
    m_fillColor.setAlpha(darkTheme ? 38 : 25);
    m_borderColor = tone;
    m_borderColor.setAlpha(darkTheme ? 64 : 48);
    m_textColor = tone;
    m_radius = tokenSet.radius.sm;
    update();
}

void TabIssueBadge::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen(m_borderColor, 1));
    painter.setBrush(m_fillColor);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);

    painter.setPen(m_textColor);
    painter.setFont(font());
    painter.drawText(contentsRect(), alignment(), text());
}

void TabIssueBadge::mouseReleaseEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton && rect().contains(event->pos()))
    {
        event->accept();
        activateOwningTab();
        return;
    }

    QLabel::mouseReleaseEvent(event);
}

void TabIssueBadge::keyPressEvent(QKeyEvent* event)
{
    if (event && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space))
    {
        event->accept();
        activateOwningTab();
        return;
    }

    QLabel::keyPressEvent(event);
}

void TabIssueBadge::activateOwningTab()
{
    auto* bar = qobject_cast<QTabBar*>(parentWidget());
    if (!bar)
    {
        return;
    }

    for (int i = 0; i < bar->count(); ++i)
    {
        if (bar->tabButton(i, QTabBar::LeftSide) == this || bar->tabButton(i, QTabBar::RightSide) == this)
        {
            bar->setCurrentIndex(i);
            return;
        }
    }
}

void applyBadgeSeverityStyle(QLabel* badge, ValidationIssue::Severity severity)
{
    if (!badge)
    {
        return;
    }

    const auto& tokenSet = an::ui::tokens();
    const auto& c = tokenSet.colors;
    const bool isError = severity == ValidationIssue::Severity::Error;

    if (auto* statusBadge = dynamic_cast<TabIssueBadge*>(badge))
    {
        statusBadge->setStatus(isError ? TabIssueBadge::Status::Error : TabIssueBadge::Status::Warning);
        statusBadge->refreshTheme();
    }
    else
    {
        QColor tone = isError ? c.statusError : c.statusWarning;
        QColor fill = tone;
        fill.setAlpha(an::ui::isDarkTheme() ? 38 : 25);
        QColor border = tone;
        border.setAlpha(an::ui::isDarkTheme() ? 64 : 48);

        badge->setFrameShape(QFrame::StyledPanel);
        QPalette pal = badge->palette();
        pal.setColor(QPalette::Window, fill);
        pal.setColor(QPalette::WindowText, tone);
        pal.setColor(QPalette::Mid, border);
        badge->setPalette(pal);
        badge->setProperty("anFrameRadius", tokenSet.radius.sm);
    }

    badge->setFont(tokenSet.typography.caption);
    badge->setContentsMargins(tokenSet.spacing.sm, tokenSet.spacing.xs, tokenSet.spacing.sm, tokenSet.spacing.xs);
    badge->setProperty("anTabIssueBadge", true);

    const int minHeight = (std::max)(16, badge->fontMetrics().height() + tokenSet.spacing.xs * 2);
    const int minWidth =
        (std::max)(minHeight, badge->fontMetrics().horizontalAdvance(QStringLiteral("99+")) + tokenSet.spacing.sm * 2);
    badge->setFixedHeight(minHeight);
    badge->setMinimumWidth(minWidth);
}
