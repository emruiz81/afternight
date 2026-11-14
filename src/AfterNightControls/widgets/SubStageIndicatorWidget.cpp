// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SubStageIndicatorWidget.h"
#include "theme/AnDesignTokens.h"
#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QToolTip>
#include <algorithm>
#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(SubStageIndicatorWidget)

SubStageIndicatorWidget::SubStageIndicatorWidget(QWidget* parent) : QFrame(parent), m_stages(kSubStageCount)
{
    setMinimumHeight(kStageHeight + 2 * kVerticalPadding);
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true); // For tooltips

    // Initialize all stages to pending
    resetAllStages();
}

void SubStageIndicatorWidget::setSubStageState(SubStage stage, SubStageState state)
{
    const int idx = static_cast<int>(stage);
    if (idx >= 0 && idx < m_stages.size())
    {
        m_stages[idx].state = state;
        updateTooltips();
        update();
    }
}

void SubStageIndicatorWidget::setSubStageStats(SubStage stage, const SubStageStats& stats)
{
    const int idx = static_cast<int>(stage);
    if (idx >= 0 && idx < m_stages.size())
    {
        m_stages[idx].stats = stats;
        updateTooltips();
        update();
    }
}

void SubStageIndicatorWidget::setAlignmentSkipped(bool skipped)
{
    m_alignmentSkipped = skipped;
    if (skipped)
    {
        setSubStageState(SubStage::Alignment, SubStageState::Skipped);
    }
    else
    {
        // If alignment was previously marked as skipped, revert it back to pending.
        // (This is mainly to keep the widget state correct if the caller toggles drizzle mode.)
        if (getSubStageState(SubStage::Alignment) == SubStageState::Skipped)
        {
            setSubStageState(SubStage::Alignment, SubStageState::Pending);
        }
    }
    update();
}

void SubStageIndicatorWidget::resetAllStages()
{
    for (int i = 0; i < m_stages.size(); ++i)
    {
        m_stages[i].state = SubStageState::Pending;
        m_stages[i].stats = SubStageStats{};
    }
    m_alignmentSkipped = false;
    updateTooltips();
    update();
}

void SubStageIndicatorWidget::setInMemoryCalibrationMode(bool enabled)
{
    if (m_inMemoryCalibrationMode != enabled)
    {
        m_inMemoryCalibrationMode = enabled;
        updateTooltips();
        update();
    }
}

SubStageIndicatorWidget::SubStageState SubStageIndicatorWidget::getSubStageState(SubStage stage) const
{
    const int idx = static_cast<int>(stage);
    if (idx >= 0 && idx < m_stages.size())
    {
        return m_stages[idx].state;
    }
    return SubStageState::Pending;
}

const SubStageIndicatorWidget::SubStageStats& SubStageIndicatorWidget::getSubStageStats(SubStage stage) const
{
    const int idx = static_cast<int>(stage);
    if (idx >= 0 && idx < m_stages.size())
    {
        return m_stages[idx].stats;
    }
    static SubStageStats empty;
    return empty;
}

bool SubStageIndicatorWidget::hasActiveStage() const
{
    for (const auto& stage : m_stages)
    {
        if (stage.state == SubStageState::Active)
        {
            return true;
        }
    }
    return false;
}

SubStageIndicatorWidget::SubStage SubStageIndicatorWidget::getActiveStage() const
{
    for (int i = 0; i < m_stages.size(); ++i)
    {
        if (m_stages[i].state == SubStageState::Active)
        {
            return static_cast<SubStage>(i);
        }
    }
    return SubStage::Calibration;
}

QString SubStageIndicatorWidget::getSubStageName(SubStage stage)
{
    switch (stage)
    {
    case SubStage::Calibration:
        return QStringLiteral("Calibration");
    case SubStage::StarDetection:
        return QStringLiteral("Star Detection");
    case SubStage::Registration:
        return QStringLiteral("Registration");
    case SubStage::Alignment:
        return QStringLiteral("Alignment");
    case SubStage::Stacking:
        return QStringLiteral("Stacking");
    default:
        return QStringLiteral("Unknown");
    }
}

QString SubStageIndicatorWidget::getSubStageShortName(SubStage stage)
{
    switch (stage)
    {
    case SubStage::Calibration:
        return QStringLiteral("Calibrate");
    case SubStage::StarDetection:
        return QStringLiteral("Detect");
    case SubStage::Registration:
        return QStringLiteral("Register");
    case SubStage::Alignment:
        return QStringLiteral("Align");
    case SubStage::Stacking:
        return QStringLiteral("Stack");
    default:
        return QStringLiteral("?");
    }
}

QSize SubStageIndicatorWidget::sizeHint() const
{
    const int totalWidth = kSubStageCount * kMinStageWidth + (kSubStageCount - 1) * kArrowWidth;
    return QSize(totalWidth, kStageHeight + 2 * kVerticalPadding);
}

QSize SubStageIndicatorWidget::minimumSizeHint() const
{
    const int totalWidth = kSubStageCount * 70 + (kSubStageCount - 1) * kArrowWidth;
    return QSize(totalWidth, kStageHeight + 2 * kVerticalPadding);
}

void SubStageIndicatorWidget::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int availableWidth = width();

    // When in-memory calibration mode is enabled, we merge Calibration and StarDetection
    // visually into a single "Calib+Detect" stage, so we draw one fewer stage.
    const int visibleStageCount = m_inMemoryCalibrationMode ? (kSubStageCount - 1) : kSubStageCount;
    const int stageAreaWidth = availableWidth - (visibleStageCount - 1) * kArrowWidth;
    const int stageWidth = (std::max)(1, stageAreaWidth / (std::max)(1, visibleStageCount));
    const int centerY = height() / 2;

    int xPos = 0;
    int drawnCount = 0;

    for (int i = 0; i < kSubStageCount; ++i)
    {
        // Skip Calibration stage in in-memory mode (it's merged with StarDetection)
        if (m_inMemoryCalibrationMode && i == static_cast<int>(SubStage::Calibration))
        {
            continue;
        }

        // Draw stage box
        QRect stageRect(xPos, kVerticalPadding, stageWidth, kStageHeight);
        drawSubStage(painter, i, stageRect);

        xPos += stageWidth;
        drawnCount++;

        // Draw arrow between stages (except after the last one)
        if (drawnCount < visibleStageCount)
        {
            drawArrow(painter, xPos, centerY);
            xPos += kArrowWidth;
        }
    }
}

void SubStageIndicatorWidget::drawSubStage(QPainter& painter, int index, const QRect& rect)
{
    if (index < 0 || index >= m_stages.size())
    {
        return;
    }

    const auto& stageData = m_stages[index];
    const SubStage stage = static_cast<SubStage>(index);
    const QColor currentStateColor = getStateColor(stageData.state);

    // Background with rounded corners
    painter.setPen(Qt::NoPen);

    // Create gradient background based on state using token-derived colors
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    {
        QColor base = currentStateColor;
        QColor hi = base;
        hi.setAlpha(stageData.state == SubStageState::Active ? 60 : 50);
        QColor lo = base;
        lo.setAlpha(stageData.state == SubStageState::Active ? 30 : 25);
        gradient.setColorAt(0, hi);
        gradient.setColorAt(1, lo);
    }

    painter.setBrush(gradient);
    painter.drawRoundedRect(rect, an::ui::tokens().radius.md, an::ui::tokens().radius.md);

    // Border
    QPen borderPen(currentStateColor, stageData.state == SubStageState::Active ? 2 : 1);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, an::ui::tokens().radius.md, an::ui::tokens().radius.md);

    // Icon and text
    const QString icon = getStateIcon(stageData.state);

    // In in-memory calibration mode, StarDetection is shown as "Calib+Detect" to indicate merged stage
    QString shortName;
    if (m_inMemoryCalibrationMode && stage == SubStage::StarDetection)
    {
        shortName = QStringLiteral("Calib+Detect");
    }
    else
    {
        shortName = getSubStageShortName(stage);
    }

    // Draw icon at top
    QFont iconFont = an::ui::tokens().typography.heading2;
    painter.setFont(iconFont);
    painter.setPen(currentStateColor);

    QRect iconRect = rect;
    iconRect.setBottom(rect.top() + rect.height() / 2 - 2);
    painter.drawText(iconRect, Qt::AlignCenter, icon);

    // Draw stage name below
    QFont nameFont = stageData.state == SubStageState::Active ? an::ui::tokens().typography.heading3
                                                              : an::ui::tokens().typography.caption;
    painter.setFont(nameFont);
    painter.setPen(stageData.state == SubStageState::Skipped ? an::ui::tokens().colors.textTertiary
                                                             : an::ui::tokens().colors.textPrimary);

    QRect nameRect = rect;
    nameRect.setTop(rect.top() + rect.height() / 2 - 2);
    nameRect.setBottom(rect.top() + rect.height() * 3 / 4);
    painter.drawText(nameRect, Qt::AlignCenter, shortName);

    // Draw count if available (e.g., "45/100")
    if (stageData.stats.totalCount > 0 || stageData.stats.processedCount > 0)
    {
        QFont countFont = an::ui::tokens().typography.caption;
        painter.setFont(countFont);
        painter.setPen(an::ui::tokens().colors.textSecondary);

        QString countText = QString("%1/%2").arg(stageData.stats.processedCount).arg(stageData.stats.totalCount);

        QRect countRect = rect;
        countRect.setTop(rect.top() + rect.height() * 3 / 4);
        painter.drawText(countRect, Qt::AlignCenter, countText);
    }
}

void SubStageIndicatorWidget::drawArrow(QPainter& painter, int x, int centerY)
{
    const int arrowSize = 6;
    const int arrowCenterX = x + kArrowWidth / 2;

    painter.setPen(QPen(an::ui::tokens().colors.borderSubtle, 1.5));
    painter.setBrush(Qt::NoBrush);

    // Draw arrow: >
    QPolygon arrow;
    arrow << QPoint(arrowCenterX - arrowSize / 2, centerY - arrowSize) << QPoint(arrowCenterX + arrowSize / 2, centerY)
          << QPoint(arrowCenterX - arrowSize / 2, centerY + arrowSize);

    painter.drawPolyline(arrow);
}

QColor SubStageIndicatorWidget::getStateColor(SubStageState state) const
{
    return stateColor(state);
}

QColor SubStageIndicatorWidget::stateColor(SubStageState state) const
{
    const auto& c = an::ui::tokens().colors;
    switch (state)
    {
    case SubStageState::Pending:
        return c.textSecondary;
    case SubStageState::Active:
        return c.accentPrimary;
    case SubStageState::Completed:
        return c.statusSuccess;
    case SubStageState::Skipped:
        return c.textTertiary;
    case SubStageState::Failed:
        return c.statusError;
    default:
        return c.textSecondary;
    }
}

void SubStageIndicatorWidget::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::PaletteChange || ev->type() == QEvent::ApplicationPaletteChange ||
        ev->type() == QEvent::ThemeChange)
        update();
    QFrame::changeEvent(ev);
}

QString SubStageIndicatorWidget::getStateIcon(SubStageState state) const
{
    switch (state)
    {
    case SubStageState::Pending:
        return QStringLiteral("⏳");
    case SubStageState::Active:
        return QStringLiteral("▶");
    case SubStageState::Completed:
        return QStringLiteral("✓");
    case SubStageState::Skipped:
        return QStringLiteral("⊘");
    case SubStageState::Failed:
        return QStringLiteral("✗");
    default:
        return QStringLiteral("?");
    }
}

void SubStageIndicatorWidget::updateTooltips()
{
    QString tooltip;
    tooltip += QStringLiteral("<html><body style='white-space:nowrap;'>");

    if (m_inMemoryCalibrationMode)
    {
        tooltip += QStringLiteral("<b>Pipeline Sub-Stages (In-Memory Calibration)</b><br/><br/>");
    }
    else
    {
        tooltip += QStringLiteral("<b>Pipeline Sub-Stages</b><br/><br/>");
    }

    for (int i = 0; i < kSubStageCount; ++i)
    {
        const auto stage = static_cast<SubStage>(i);
        const auto& data = m_stages[i];

        // Skip Calibration in tooltip when in-memory mode (it's merged with StarDetection)
        if (m_inMemoryCalibrationMode && stage == SubStage::Calibration)
        {
            continue;
        }

        QString stateStr;
        QString color;
        switch (data.state)
        {
        case SubStageState::Pending:
            stateStr = QStringLiteral("Pending");
            color = QStringLiteral("#aaaaaa");
            break;
        case SubStageState::Active:
            stateStr = QStringLiteral("Active");
            color = QStringLiteral("#5294e2");
            break;
        case SubStageState::Completed:
            stateStr = QStringLiteral("Completed");
            color = QStringLiteral("#6db86d");
            break;
        case SubStageState::Skipped:
            stateStr = QStringLiteral("Skipped");
            color = QStringLiteral("#808080");
            break;
        case SubStageState::Failed:
            stateStr = QStringLiteral("Failed");
            color = QStringLiteral("#dc5050");
            break;
        }

        // Use merged name for StarDetection in in-memory mode
        QString stageName;
        if (m_inMemoryCalibrationMode && stage == SubStage::StarDetection)
        {
            stageName = QStringLiteral("Calibration + Star Detection");
        }
        else
        {
            stageName = getSubStageName(stage);
        }

        tooltip += QStringLiteral("<span style='color:%1'>%2</span>: %3").arg(color).arg(stageName).arg(stateStr);

        if (data.stats.totalCount > 0)
        {
            tooltip += QStringLiteral(" (%1/%2)").arg(data.stats.processedCount).arg(data.stats.totalCount);
            if (data.stats.failedCount > 0)
            {
                tooltip += QStringLiteral(" <span style='color:#dc5050'>%1 failed</span>").arg(data.stats.failedCount);
            }
        }

        tooltip += QStringLiteral("<br/>");
    }

    tooltip += QStringLiteral("</body></html>");
    setToolTip(tooltip);
}
