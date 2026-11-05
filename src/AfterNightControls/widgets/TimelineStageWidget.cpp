// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TimelineStageWidget.h"
#include "app/UiStyleTokens.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QtGlobal>
#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(TimelineStageWidget)

namespace
{

QString toCssColor(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QColor blend(const QColor& base, const QColor& overlay, const qreal overlayWeight)
{
    const qreal w = qBound(0.0, overlayWeight, 1.0);
    const qreal inv = 1.0 - w;
    return QColor(qRound(base.red() * inv + overlay.red() * w), qRound(base.green() * inv + overlay.green() * w),
                  qRound(base.blue() * inv + overlay.blue() * w));
}

QColor timelineStateColor(const an::ui::styling::ThemeVariant themeVariant, const TimelineStageWidget::StageState state)
{
    using an::ui::styling::SemanticColorRole;
    switch (state)
    {
    case TimelineStageWidget::StageState::Completed:
        return an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusSuccess);
    case TimelineStageWidget::StageState::Active:
        return an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusActive);
    case TimelineStageWidget::StageState::Disabled:
    case TimelineStageWidget::StageState::Pending:
        return an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusPending);
    }

    return an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusPending);
}

} // namespace

TimelineStageWidget::TimelineStageWidget(QWidget* parent) : QFrame(parent), m_state(StageState::Pending)
{
    setMinimumHeight(90);
    setFrameShape(QFrame::Box);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TimelineStageWidget::setStageName(const QString& name)
{
    m_stageName = name;
    updateTooltip();
    update();
}

void TimelineStageWidget::setGroupInfo(const QString& info)
{
    m_groupInfo = info;
    updateTooltip();
    update();
}

void TimelineStageWidget::setElapsedTime(const QString& time)
{
    m_elapsedTime = time;
    updateTooltip();
    update();
}

void TimelineStageWidget::setState(StageState state)
{
    m_state = state;
    updateTooltip();
    update();
}

void TimelineStageWidget::updateTooltip()
{
    const auto themeVariant = an::ui::styling::detectThemeVariant(palette());

    QString stateStr;
    QString stateColor;
    QString stateIcon;
    QString stateDescription;

    switch (m_state)
    {
    case StageState::Disabled:
        stateStr = QStringLiteral("Disabled");
        stateColor = toCssColor(timelineStateColor(themeVariant, StageState::Disabled));
        stateIcon = QStringLiteral("○");
        stateDescription = QStringLiteral("This stage is not included in the current workflow");
        break;
    case StageState::Pending:
        stateStr = QStringLiteral("Pending");
        stateColor = toCssColor(timelineStateColor(themeVariant, StageState::Pending));
        stateIcon = QStringLiteral("⏳");
        stateDescription = QStringLiteral("Waiting to start processing");
        break;
    case StageState::Active:
        stateStr = QStringLiteral("Active");
        stateColor = toCssColor(timelineStateColor(themeVariant, StageState::Active));
        stateIcon = QStringLiteral("▶");
        stateDescription = QStringLiteral("Currently processing this stage");
        break;
    case StageState::Completed:
        stateStr = QStringLiteral("Completed");
        stateColor = toCssColor(timelineStateColor(themeVariant, StageState::Completed));
        stateIcon = QStringLiteral("✓");
        stateDescription = QStringLiteral("Stage finished successfully");
        break;
    }

    QString tooltip;
    tooltip += QString("<b>%1 Stage</b><br/>").arg(m_stageName);
    tooltip += QStringLiteral("<hr/>");
    tooltip += QString("<b>Status:</b> <span style='color:%1;'>%2 %3</span><br/>").arg(stateColor, stateIcon, stateStr);
    tooltip += QString("<i>%1</i><br/><br/>").arg(stateDescription);

    if (m_state != StageState::Disabled)
    {
        // Parse group info for more detailed display
        if (!m_groupInfo.isEmpty() && m_groupInfo != QStringLiteral("0/0") && m_groupInfo != QStringLiteral("—"))
        {
            // Check for LIGHTS dual-phase format: "R:X/Y S:A/B"
            if (m_groupInfo.contains(QStringLiteral("R:")) && m_groupInfo.contains(QStringLiteral("S:")))
            {
                tooltip += QStringLiteral("<b>Progress:</b><br/>");
                // Parse "R:X/Y S:A/B" format
                QRegularExpression rx(QStringLiteral("R:(\\d+)/(\\d+)\\s+S:(\\d+)/(\\d+)"));
                QRegularExpressionMatch match = rx.match(m_groupInfo);
                if (match.hasMatch())
                {
                    tooltip +=
                        QString("  • Registration: %1 of %2 groups<br/>").arg(match.captured(1), match.captured(2));
                    tooltip += QString("  • Stacking: %1 of %2 groups<br/>").arg(match.captured(3), match.captured(4));
                }
                else
                {
                    tooltip += QString("  • Groups: %1<br/>").arg(m_groupInfo);
                }
            }
            else
            {
                // Standard "X/Y" format
                QStringList parts = m_groupInfo.split(QStringLiteral("/"));
                if (parts.size() == 2)
                {
                    bool okCurrent = false, okTotal = false;
                    int current = parts[0].toInt(&okCurrent);
                    int total = parts[1].toInt(&okTotal);
                    if (okCurrent && okTotal && total > 0)
                    {
                        int percent = (current * 100) / total;
                        tooltip += QString("<b>Groups:</b> %1 of %2 (%3%)<br/>").arg(current).arg(total).arg(percent);
                    }
                    else
                    {
                        tooltip += QString("<b>Groups:</b> %1<br/>").arg(m_groupInfo);
                    }
                }
                else
                {
                    tooltip += QString("<b>Groups:</b> %1<br/>").arg(m_groupInfo);
                }
            }
        }
        else if (m_state == StageState::Pending)
        {
            tooltip += QStringLiteral("<b>Groups:</b> Not started yet<br/>");
        }

        if (!m_elapsedTime.isEmpty() && m_elapsedTime != QStringLiteral("—"))
        {
            tooltip += QString("<b>Elapsed Time:</b> %1<br/>").arg(m_elapsedTime);
        }
        else if (m_state == StageState::Completed)
        {
            tooltip += QStringLiteral("<b>Elapsed Time:</b> (completed)<br/>");
        }
    }

    setToolTip(tooltip);
}

QSize TimelineStageWidget::sizeHint() const
{
    return QSize(200, 90);
}

QSize TimelineStageWidget::minimumSizeHint() const
{
    return QSize(150, 90);
}

void TimelineStageWidget::paintEvent(QPaintEvent* event)
{
    using an::ui::styling::SemanticColorRole;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto themeVariant = an::ui::styling::detectThemeVariant(palette());

    // Draw background gradient
    QLinearGradient gradient = getBackgroundGradient();
    painter.fillRect(rect(), gradient);

    // Draw border
    QPen borderPen(an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceBorder), 1);
    if (m_state == StageState::Active)
    {
        borderPen.setColor(an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusActive));
        borderPen.setWidth(2);
    }
    painter.setPen(borderPen);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // Apply opacity for disabled state
    if (m_state == StageState::Disabled)
    {
        painter.setOpacity(0.4);
    }

    // Draw icon
    QPixmap iconPixmap = getIconForState();
    if (!iconPixmap.isNull())
    {
        QRect iconRect(rect().center().x() - 9, 10, 18, 18);
        painter.drawPixmap(iconRect, iconPixmap);
    }

    // Draw stage name
    painter.setPen(an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextPrimary));
    QFont nameFont = an::ui::tokens().typography.heading3;
    painter.setFont(nameFont);
    QRect nameRect(0, 34, rect().width(), 16);
    painter.drawText(nameRect, Qt::AlignCenter, m_stageName);

    // Draw group info
    painter.setPen(an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextSecondary));
    QFont infoFont = an::ui::tokens().typography.body;
    painter.setFont(infoFont);
    QRect infoRect(0, 52, rect().width(), 14);
    painter.drawText(infoRect, Qt::AlignCenter, m_groupInfo);

    // Draw elapsed time slightly above the bottom edge
    painter.setPen(an::ui::styling::semanticColor(themeVariant, SemanticColorRole::TextMuted));
    QFont timeFont = an::ui::tokens().typography.body;
    painter.setFont(timeFont);
    const int timeHeight = 14;
    const int bottomMargin = 10;
    QRect timeRect(0, rect().height() - bottomMargin - timeHeight, rect().width(), timeHeight);
    painter.drawText(timeRect, Qt::AlignCenter, m_elapsedTime);

    QFrame::paintEvent(event);
}

QString TimelineStageWidget::getIconPathForState() const
{
    switch (m_state)
    {
    case StageState::Completed:
        return QStringLiteral(":/AfterNight/icons/SVG/timeline_check_circle.svg");
    case StageState::Active:
        return QStringLiteral(":/AfterNight/icons/SVG/timeline_play_arrow.svg");
    case StageState::Pending:
    case StageState::Disabled:
        return QStringLiteral(":/AfterNight/icons/SVG/timeline_radio_unchecked.svg");
    }
    return QString();
}

QPixmap TimelineStageWidget::getIconForState() const
{
    QString iconPath = getIconPathForState();
    if (iconPath.isEmpty())
    {
        return QPixmap();
    }

    auto* context = an::ui::appContext();
    if (!context)
    {
        return QPixmap();
    }

    auto iconProvider = context->getIconProvider();
    if (!iconProvider)
    {
        return QPixmap();
    }

    return iconProvider->loadSvgFileAsPixmap(iconPath, QSize(18, 18), getIconColor());
}

QColor TimelineStageWidget::getIconColor() const
{
    return timelineStateColor(an::ui::styling::detectThemeVariant(palette()), m_state);
}

QLinearGradient TimelineStageWidget::getBackgroundGradient() const
{
    using an::ui::styling::SemanticColorRole;

    const auto themeVariant = an::ui::styling::detectThemeVariant(palette());
    const bool darkMode = (themeVariant == an::ui::styling::ThemeVariant::Dark);
    const QColor base = an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceElevated);

    QLinearGradient gradient(0, 0, 0, rect().height());

    switch (m_state)
    {
    case StageState::Completed:
        gradient.setColorAt(0,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusSuccess),
                                  darkMode ? 0.30 : 0.18));
        gradient.setColorAt(1,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusSuccess),
                                  darkMode ? 0.20 : 0.10));
        break;
    case StageState::Active:
        gradient.setColorAt(0,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusActive),
                                  darkMode ? 0.30 : 0.18));
        gradient.setColorAt(1,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::StatusActive),
                                  darkMode ? 0.20 : 0.10));
        break;
    case StageState::Pending:
        gradient.setColorAt(0,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceOverlay),
                                  darkMode ? 0.35 : 0.20));
        gradient.setColorAt(1,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceOverlay),
                                  darkMode ? 0.35 : 0.20));
        break;
    case StageState::Disabled:
        gradient.setColorAt(0,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceOverlay),
                                  darkMode ? 0.50 : 0.30));
        gradient.setColorAt(1,
                            blend(base, an::ui::styling::semanticColor(themeVariant, SemanticColorRole::SurfaceOverlay),
                                  darkMode ? 0.50 : 0.30));
        break;
    }

    return gradient;
}
