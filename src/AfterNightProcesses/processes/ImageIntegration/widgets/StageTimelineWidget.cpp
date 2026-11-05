// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StageTimelineWidget.h"
#include "theme/AnDesignTokens.h"

#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QMouseEvent>
#include <QEvent>
#include <vector>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(StageTimelineWidget)

struct StageInfo
{
    QString id; // Original identifier (e.g. "LightsRegistration")
    QString displayName; // Friendly name (e.g. "Registration")
    StageTimelineStatus status = StageTimelineStatus::Pending;
    qint64 elapsedTimeMs = 0;
};

class StageTimelineWidget::Impl
{
public:
    std::vector<StageInfo> stages;
    int currentStageIndex = -1;

    // Style constants
    const int kNodeRadius = 10;
    const int kLineWidth = 3;
    const int kTextMargin = 10;
    const int kMinStepWidth = 130;
    const int kTextWidth = 120;
    const int kCenterY = 30;

    [[nodiscard]] QColor pendingColor() const
    {
        return an::ui::tokens().colors.textTertiary;
    }
    [[nodiscard]] QColor inProgressColor() const
    {
        return an::ui::tokens().colors.statusInfo;
    }
    [[nodiscard]] QColor completedColor() const
    {
        return an::ui::tokens().colors.statusSuccess;
    }
    [[nodiscard]] QColor skippedColor() const
    {
        return an::ui::tokens().colors.textSecondary;
    }
    [[nodiscard]] QColor failedColor() const
    {
        return an::ui::tokens().colors.statusError;
    }
    [[nodiscard]] QColor markerTextOnStatusColor() const
    {
        return an::ui::tokens().colors.textOnAccent;
    }
    [[nodiscard]] QColor durationTextColor() const
    {
        return an::ui::tokens().colors.textSecondary;
    }

    QColor colorForStatus(StageTimelineStatus status) const
    {
        switch (status)
        {
        case StageTimelineStatus::Pending:
            return pendingColor();
        case StageTimelineStatus::InProgress:
            return inProgressColor();
        case StageTimelineStatus::Completed:
            return completedColor();
        case StageTimelineStatus::Skipped:
            return skippedColor();
        case StageTimelineStatus::Failed:
            return failedColor();
        }
        return pendingColor();
    }

    QString beautifyName(const QString& name) const
    {
        // Simple mapping based on known stage names
        if (name.contains(QStringLiteral("LightsCalibration"), Qt::CaseInsensitive))
            return QStringLiteral("Calibration");
        if (name.contains(QStringLiteral("LightsRegistration"), Qt::CaseInsensitive))
            return QStringLiteral("Registration");
        if (name.contains(QStringLiteral("LightsAlignment"), Qt::CaseInsensitive))
            return QStringLiteral("Alignment");
        if (name.contains(QStringLiteral("LightsNormalization"), Qt::CaseInsensitive))
            return QStringLiteral("Normalization");
        if (name.contains(QStringLiteral("LightsDrizzle"), Qt::CaseInsensitive))
            return QStringLiteral("Drizzle");
        if (name.contains(QStringLiteral("LightsStacking"), Qt::CaseInsensitive))
            return QStringLiteral("Stacking");
        if (name.contains(QStringLiteral("Bias"), Qt::CaseInsensitive))
            return QStringLiteral("Bias");
        if (name.contains(QStringLiteral("Dark"), Qt::CaseInsensitive))
            return QStringLiteral("Dark");
        if (name.contains(QStringLiteral("Flat"), Qt::CaseInsensitive))
            return QStringLiteral("Flat");
        return name;
    }

    QString getStageDescription(const QString& id) const
    {
        if (id.contains(QStringLiteral("Bias"), Qt::CaseInsensitive))
            return QStringLiteral("Integrates bias frames to create a master bias reference.");
        if (id.contains(QStringLiteral("Dark"), Qt::CaseInsensitive))
            return QStringLiteral("Integrates dark frames to create a master dark reference.");
        if (id.contains(QStringLiteral("Flat"), Qt::CaseInsensitive))
            return QStringLiteral("Integrates flat frames to create a master flat reference.");
        if (id.contains(QStringLiteral("Calibration"), Qt::CaseInsensitive))
            return QStringLiteral("Subtracts bias/dark and corrects flat field for light frames.");
        if (id.contains(QStringLiteral("Registration"), Qt::CaseInsensitive))
            return QStringLiteral("Detects stars and computes alignment transformations.");
        if (id.contains(QStringLiteral("Alignment"), Qt::CaseInsensitive))
            return QStringLiteral("Applies transformations to generate aligned light frames.");
        if (id.contains(QStringLiteral("Normalization"), Qt::CaseInsensitive))
            return QStringLiteral("Matches background and signal levels across all frames.");
        if (id.contains(QStringLiteral("Drizzle"), Qt::CaseInsensitive))
            return QStringLiteral("Performs Drizzle integration for high-resolution stacking.");
        if (id.contains(QStringLiteral("Stacking"), Qt::CaseInsensitive))
            return QStringLiteral("Integrates registered frames into the final image.");
        return QStringLiteral("Workflow processing stage.");
    }

    QString generateTooltip(const StageInfo& stage) const
    {
        QString html = QStringLiteral("<b>%1</b>").arg(stage.displayName);
        html += QStringLiteral("<br><i>%1</i>").arg(getStageDescription(stage.id));
        html += QStringLiteral("<hr>");

        QString statusStr;
        switch (stage.status)
        {
        case StageTimelineStatus::Pending:
            statusStr = QStringLiteral("Pending");
            break;
        case StageTimelineStatus::InProgress:
            statusStr = QStringLiteral("In Progress...");
            break;
        case StageTimelineStatus::Completed:
            statusStr = QStringLiteral("Completed");
            break;
        case StageTimelineStatus::Skipped:
            statusStr = QStringLiteral("Skipped");
            break;
        case StageTimelineStatus::Failed:
            statusStr = QStringLiteral("Failed");
            break;
        }

        html += QStringLiteral("Status: <b>%1</b>").arg(statusStr);

        if (stage.elapsedTimeMs > 0)
        {
            qint64 s = stage.elapsedTimeMs / 1000;
            if (s < 60)
                html += QStringLiteral("<br>Duration: %1s").arg(s);
            else
                html += QStringLiteral("<br>Duration: %1m %2s").arg(s / 60).arg(s % 60);
        }

        return html;
    }
};

StageTimelineWidget::StageTimelineWidget(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    setMinimumHeight(85);
    setMouseTracking(true); // Enable mouse tracking for tooltips
}

StageTimelineWidget::~StageTimelineWidget() = default;

void StageTimelineWidget::setStages(const QStringList& stageNames)
{
    m_impl->stages.clear();
    for (const auto& name : stageNames)
    {
        m_impl->stages.push_back({name, m_impl->beautifyName(name), StageTimelineStatus::Pending, 0});
    }
    m_impl->currentStageIndex = 0;
    updateGeometry();
    update();
}

void StageTimelineWidget::setCurrentStage(int index)
{
    if (index >= 0 && index < static_cast<int>(m_impl->stages.size()))
    {
        m_impl->currentStageIndex = index;
        update();
    }
}

void StageTimelineWidget::setStageStatus(int index, StageTimelineStatus status)
{
    if (index >= 0 && index < static_cast<int>(m_impl->stages.size()))
    {
        m_impl->stages[static_cast<size_t>(index)].status = status;
        update();
    }
}

void StageTimelineWidget::setStageElapsedTime(int index, qint64 milliseconds)
{
    if (index >= 0 && index < static_cast<int>(m_impl->stages.size()))
    {
        m_impl->stages[static_cast<size_t>(index)].elapsedTimeMs = milliseconds;
        update();
    }
}

int StageTimelineWidget::currentStage() const
{
    return m_impl->currentStageIndex;
}

StageTimelineStatus StageTimelineWidget::stageStatus(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_impl->stages.size()))
    {
        return m_impl->stages[static_cast<size_t>(index)].status;
    }
    return StageTimelineStatus::Pending;
}

QPoint StageTimelineWidget::stageCenter(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_impl->stages.size()))
        return QPoint();

    const int count = static_cast<int>(m_impl->stages.size());
    const int sideMargin = m_impl->kTextWidth / 2;
    int availableWidth = width() - 2 * sideMargin;

    double stepWidth = 0.0;
    if (count > 1)
    {
        stepWidth = static_cast<double>(availableWidth) / (count - 1);
    }

    int x = sideMargin + static_cast<int>(index * stepWidth);
    return QPoint(x, m_impl->kCenterY);
}

QSize StageTimelineWidget::sizeHint() const
{
    // Just return a reasonable default, let layout handle width
    return QSize(800, 100);
}

QSize StageTimelineWidget::minimumSizeHint() const
{
    return QSize(400, 85);
}

void StageTimelineWidget::paintEvent(QPaintEvent*)
{
    if (m_impl->stages.empty())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int count = static_cast<int>(m_impl->stages.size());
    const int centerY = m_impl->kCenterY;

    const int sideMargin = m_impl->kTextWidth / 2;
    int availableWidth = width() - 2 * sideMargin;

    double stepWidth = 0.0;
    if (count > 1)
    {
        stepWidth = static_cast<double>(availableWidth) / (count - 1);
    }

    int startX = sideMargin;

    // Draw connecting lines first
    for (int i = 0; i < count - 1; ++i)
    {
        int x1 = startX + static_cast<int>(i * stepWidth);
        int x2 = startX + static_cast<int>((i + 1) * stepWidth);

        QColor lineColor = m_impl->pendingColor();
        if (m_impl->stages[static_cast<size_t>(i + 1)].status != StageTimelineStatus::Pending)
        {
            lineColor = m_impl->completedColor();
            if (m_impl->stages[static_cast<size_t>(i + 1)].status == StageTimelineStatus::InProgress)
                lineColor = m_impl->inProgressColor();
        }

        p.setPen(QPen(lineColor, m_impl->kLineWidth));
        p.drawLine(x1, centerY, x2, centerY);
    }

    // Draw nodes and labels
    for (int i = 0; i < count; ++i)
    {
        const auto& stage = m_impl->stages[static_cast<size_t>(i)];
        int x = startX + static_cast<int>(i * stepWidth);

        QColor color = m_impl->colorForStatus(stage.status);

        if (i == m_impl->currentStageIndex && stage.status == StageTimelineStatus::InProgress)
        {
            QColor halo = color;
            halo.setAlpha(60);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(QPoint(x, centerY), m_impl->kNodeRadius + 6, m_impl->kNodeRadius + 6);
        }

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPoint(x, centerY), m_impl->kNodeRadius, m_impl->kNodeRadius);

        p.setPen(QPen(m_impl->markerTextOnStatusColor(), 2));
        p.setBrush(Qt::NoBrush);

        if (stage.status == StageTimelineStatus::Completed)
        {
            QPainterPath path;
            path.moveTo(x - 4, centerY);
            path.lineTo(x - 1, centerY + 3);
            path.lineTo(x + 4, centerY - 4);
            p.drawPath(path);
        }
        else if (stage.status == StageTimelineStatus::Skipped)
        {
            p.drawLine(x - 4, centerY, x + 4, centerY);
        }
        else if (stage.status == StageTimelineStatus::Failed)
        {
            p.drawLine(x - 3, centerY - 3, x + 3, centerY + 3);
            p.drawLine(x - 3, centerY + 3, x + 3, centerY - 3);
        }
        else if (stage.status == StageTimelineStatus::InProgress)
        {
            p.setBrush(m_impl->markerTextOnStatusColor());
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(x, centerY), 3, 3);
        }

        const auto& tokenSet = an::ui::tokens();
        QFont labelFont = (i == m_impl->currentStageIndex) ? tokenSet.typography.heading3 : tokenSet.typography.caption;
        p.setFont(labelFont);

        p.setPen(palette().text().color());

        int textX = x - m_impl->kTextWidth / 2;
        int textY = centerY + m_impl->kNodeRadius + m_impl->kTextMargin;
        QRect textRect(textX, textY, m_impl->kTextWidth, 60);

        p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, stage.displayName);

        if (stage.status == StageTimelineStatus::Completed && stage.elapsedTimeMs > 0)
        {
            QFont durationFont = tokenSet.typography.caption;
            p.setFont(durationFont);
            p.setPen(m_impl->durationTextColor());
            const qint64 totalSeconds = stage.elapsedTimeMs / 1000;
            const qint64 minutes = totalSeconds / 60;
            const qint64 seconds = totalSeconds % 60;
            QString duration =
                minutes > 0 ? QStringLiteral("%1m %2s").arg(minutes).arg(seconds) : QStringLiteral("%1s").arg(seconds);
            QRect durationRect(textX, textY + 60, m_impl->kTextWidth, 18);
            p.drawText(durationRect, Qt::AlignHCenter | Qt::AlignTop, duration);
        }
    }
}

bool StageTimelineWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* helpEvent = static_cast<QHelpEvent*>(event);

        // Find which stage is under mouse
        const int count = static_cast<int>(m_impl->stages.size());
        const int sideMargin = m_impl->kTextWidth / 2;
        int availableWidth = width() - 2 * sideMargin;

        double stepWidth = 0.0;
        if (count > 1)
        {
            stepWidth = static_cast<double>(availableWidth) / (count - 1);
        }

        int startX = sideMargin;
        int centerY = m_impl->kCenterY;

        // Check nodes
        for (int i = 0; i < count; ++i)
        {
            int x = startX + static_cast<int>(i * stepWidth);
            QPoint center(x, centerY);

            // Check if mouse is near node (radius + some margin)
            QPoint diff = helpEvent->pos() - center;
            if (diff.manhattanLength() < m_impl->kNodeRadius + 10)
            {
                QToolTip::showText(helpEvent->globalPos(),
                                   m_impl->generateTooltip(m_impl->stages[static_cast<size_t>(i)]));
                return true;
            }

            // Also check text rect
            int textX = x - m_impl->kTextWidth / 2;
            int textY = centerY + m_impl->kNodeRadius + m_impl->kTextMargin;
            QRect textRect(textX, textY, m_impl->kTextWidth, 80);
            if (textRect.contains(helpEvent->pos()))
            {
                QToolTip::showText(helpEvent->globalPos(),
                                   m_impl->generateTooltip(m_impl->stages[static_cast<size_t>(i)]));
                return true;
            }
        }

        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}
