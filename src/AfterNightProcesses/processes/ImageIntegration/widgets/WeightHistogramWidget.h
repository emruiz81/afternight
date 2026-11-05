// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "theme/AnDesignTokens.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

/// Histogram of the previewed per-frame stacking weights, with hover details and
/// a statistics overlay. Promoted verbatim out of FrameQualityDashboard.cpp so both
/// of its translation units can address the concrete type.
// 20 bins provides more detail for weight distributions.
constexpr int kHistogramBins = 20;

class WeightHistogramWidget : public QWidget
{
public:
    explicit WeightHistogramWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(140);
        setMouseTracking(true);
        setToolTip(tr("Shows how preview weights are distributed.\n"
                      "Left = lower contribution, right = higher contribution."));
    }

    void setMeanNormalizedSemantics(const bool enabled)
    {
        m_meanNormalizedWeights = enabled;
    }

    void setWeights(const QVector<double>& weights)
    {
        m_weights = weights;
        m_bins.fill(0);

        m_plotMin = 0.0;
        m_plotMax = 1.0;
        if (m_meanNormalizedWeights && !weights.isEmpty())
        {
            double minWeight = std::numeric_limits<double>::infinity();
            double maxWeight = -std::numeric_limits<double>::infinity();
            for (const double weight : weights)
            {
                if (!std::isfinite(weight))
                {
                    continue;
                }
                minWeight = (std::min)(minWeight, weight);
                maxWeight = (std::max)(maxWeight, weight);
            }

            if (std::isfinite(minWeight) && std::isfinite(maxWeight))
            {
                if (maxWeight <= minWeight)
                {
                    const double padding = (std::max)(0.05, std::abs(minWeight) * 0.1);
                    m_plotMin = (std::max)(0.0, minWeight - padding);
                    m_plotMax = maxWeight + padding;
                }
                else
                {
                    m_plotMin = minWeight;
                    m_plotMax = maxWeight;
                }
            }
        }

        for (double w : weights)
        {
            if (!std::isfinite(w))
            {
                continue;
            }
            const double clamped = std::clamp(w, m_plotMin, m_plotMax);
            int idx = 0;
            if (m_plotMax > m_plotMin)
            {
                idx = static_cast<int>(((clamped - m_plotMin) / (m_plotMax - m_plotMin)) *
                                       static_cast<double>(kHistogramBins));
            }
            if (idx >= kHistogramBins)
            {
                idx = kHistogramBins - 1;
            }
            if (idx < 0)
            {
                idx = 0;
            }
            ++m_bins[static_cast<size_t>(idx)];
        }
        updateToolTip();
        update();
    }

    void setSelectedFrameWeight(double weight)
    {
        m_selectedWeight = weight;
        update();
    }

    void setMinWeightFloor(double floor)
    {
        m_minFloor = floor;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), palette().base());

        const int w = width();
        const int h = height();
        if (w <= 0 || h <= 0)
        {
            return;
        }

        const int marginLeft = 24;
        const int marginRight = 14;
        // Reserve space for the statistics overlay above the plot.
        // Without this, the top-right text can be clipped (negative y).
        QFont smallFont = an::ui::tokens().typography.caption;
        const int statsHeight = (std::max)(14, QFontMetrics(smallFont).height() + 2);
        const int marginTop = 6 + statsHeight;
        const int marginBottom = 18;

        const QRect plotRect(marginLeft, marginTop, w - marginLeft - marginRight, h - marginTop - marginBottom);
        if (plotRect.width() <= 0 || plotRect.height() <= 0)
        {
            return;
        }

        int maxCount = 0;
        for (int c : m_bins)
        {
            maxCount = (std::max)(maxCount, c);
        }
        if (maxCount <= 0)
        {
            p.setPen(palette().text().color());
            p.drawText(rect(), Qt::AlignCenter, tr("No weights to preview"));
            return;
        }

        // Axes.
        p.setPen(QPen(palette().mid().color(), 1.0));
        p.drawLine(plotRect.bottomLeft(), plotRect.bottomRight());
        p.drawLine(plotRect.bottomLeft(), plotRect.topLeft());

        // Grid lines (horizontal).
        p.setPen(QPen(palette().mid().color(), 1.0, Qt::DotLine));
        for (int g = 1; g <= 3; ++g)
        {
            const int y = plotRect.bottom() - (plotRect.height() * g) / 4;
            p.drawLine(plotRect.left(), y, plotRect.right(), y);
        }

        p.setPen(palette().text().color());
        p.drawText(QRect(0, plotRect.bottom() + 2, marginLeft - 4, marginBottom), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(m_plotMin, 'f', m_meanNormalizedWeights ? 2 : 0));
        p.drawText(QRect(plotRect.left() + plotRect.width() / 2 - 8, plotRect.bottom() + 2, 16, marginBottom),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number((m_plotMin + m_plotMax) * 0.5, 'f', m_meanNormalizedWeights ? 2 : 1));
        p.drawText(QRect(plotRect.right() + 2, plotRect.bottom() + 2, marginRight + 24, marginBottom),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_plotMax, 'f', m_meanNormalizedWeights ? 2 : 0));

        const double barSpacing = static_cast<double>(plotRect.width()) / kHistogramBins;
        const int barWidth = (std::max)(2, static_cast<int>(barSpacing) - 2);

        const QColor baseBarColor = palette().highlight().color();

        for (int i = 0; i < kHistogramBins; ++i)
        {
            const double frac = static_cast<double>(m_bins[static_cast<size_t>(i)]) / static_cast<double>(maxCount);
            const int barHeight = static_cast<int>(frac * static_cast<double>(plotRect.height() - 2));
            const int x = plotRect.left() + static_cast<int>(i * barSpacing);
            const int y = plotRect.bottom() - barHeight;

            if (barHeight > 0)
            {
                // Gradient for bars
                QLinearGradient grad(x, y, x, plotRect.bottom());
                grad.setColorAt(0.0, baseBarColor.lighter(130));
                grad.setColorAt(1.0, baseBarColor);

                p.setPen(Qt::NoPen);
                p.setBrush(grad);
                p.drawRoundedRect(QRect(x + 1, y, barWidth, barHeight), an::ui::tokens().radius.sm,
                                  an::ui::tokens().radius.sm);

                // Border for the hovered bin
                if (i == m_hoverBin)
                {
                    p.setPen(QPen(palette().text().color(), 1.0));
                    p.setBrush(Qt::NoBrush);
                    p.drawRoundedRect(QRect(x + 1, y, barWidth, barHeight), an::ui::tokens().radius.sm,
                                      an::ui::tokens().radius.sm);
                }
            }
        }

        // Draw min weight floor line
        if (m_minFloor > 0.0 && m_plotMax > m_plotMin && m_minFloor >= m_plotMin && m_minFloor <= m_plotMax)
        {
            const int floorX = plotRect.left() + static_cast<int>(((m_minFloor - m_plotMin) / (m_plotMax - m_plotMin)) *
                                                                  plotRect.width());

            p.setPen(QPen(an::ui::tokens().colors.statusWarning, 1, Qt::DashLine));
            p.drawLine(floorX, plotRect.top(), floorX, plotRect.bottom());
        }

        // Draw selected frame indicator
        if (std::isfinite(m_selectedWeight))
        {
            const double clampedWeight = std::clamp(m_selectedWeight, m_plotMin, m_plotMax);
            const int selX =
                plotRect.left() +
                static_cast<int>(((clampedWeight - m_plotMin) / (m_plotMax - m_plotMin)) * plotRect.width());

            p.setPen(QPen(palette().text().color(), 2));
            p.drawLine(selX, plotRect.top(), selX, plotRect.bottom());

            // Triangle marker at top
            QPolygon triangle;
            triangle << QPoint(selX - 4, plotRect.top()) << QPoint(selX + 4, plotRect.top())
                     << QPoint(selX, plotRect.top() + 6);
            p.setBrush(palette().text());
            p.drawPolygon(triangle);
        }

        // Statistics overlay
        if (!m_weights.isEmpty())
        {
            paintStatisticsOverlay(p, plotRect, statsHeight);
        }

        // Hover tooltip
        if (m_hovering && m_hoverBin >= 0)
        {
            paintHoverInfo(p);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        m_hoverPos = event->pos();
        m_hovering = true;

        const int marginLeft = 24;
        const int marginRight = 8;
        const int x = event->pos().x() - marginLeft;
        const double barSpacing = static_cast<double>(width() - marginLeft - marginRight) / kHistogramBins;

        if (x >= 0 && barSpacing > 0)
        {
            m_hoverBin = std::clamp(static_cast<int>(x / barSpacing), 0, kHistogramBins - 1);
        }
        else
        {
            m_hoverBin = -1;
        }

        update();
    }

    void leaveEvent(QEvent*) override
    {
        m_hovering = false;
        m_hoverBin = -1;
        update();
    }

private:
    void paintStatisticsOverlay(QPainter& p, const QRect& plotRect, int statsHeight)
    {
        double minW = std::numeric_limits<double>::infinity();
        double maxW = -std::numeric_limits<double>::infinity();
        double sumW = 0.0;
        int count = 0;
        for (double w : m_weights)
        {
            if (!std::isfinite(w))
                continue;
            minW = (std::min)(minW, w);
            maxW = (std::max)(maxW, w);
            sumW += w;
            ++count;
        }
        if (count <= 0)
            return;
        const double avgW = sumW / count;

        p.setPen(palette().text().color());
        QFont smallFont = an::ui::tokens().typography.caption;
        p.setFont(smallFont);

        const QString stats =
            tr("Min: %1  Avg: %2  Max: %3").arg(minW, 0, 'f', 3).arg(avgW, 0, 'f', 3).arg(maxW, 0, 'f', 3);

        const QRect statsRect(plotRect.left(), plotRect.top() - statsHeight, plotRect.width(), statsHeight);
        const QString elided = p.fontMetrics().elidedText(stats, Qt::ElideMiddle, statsRect.width() - 6);

        // Subtle background so text remains readable over the plot.
        p.setPen(Qt::NoPen);
        QColor bg = palette().window().color();
        bg.setAlpha(210);
        p.setBrush(bg);
        p.drawRoundedRect(statsRect.adjusted(0, 0, -1, -1), an::ui::tokens().radius.sm, an::ui::tokens().radius.sm);

        p.setPen(palette().text().color());
        p.drawText(statsRect.adjusted(3, 0, -3, 0), Qt::AlignRight | Qt::AlignVCenter, elided);
    }

    void paintHoverInfo(QPainter& p)
    {
        if (m_hoverBin < 0)
            return;

        const int count = m_bins[static_cast<size_t>(m_hoverBin)];
        const double binWidth =
            (m_plotMax > m_plotMin) ? (m_plotMax - m_plotMin) / static_cast<double>(kHistogramBins) : 0.0;
        const double rangeStart = m_plotMin + static_cast<double>(m_hoverBin) * binWidth;
        const double rangeEnd = m_plotMin + static_cast<double>(m_hoverBin + 1) * binWidth;

        const QString text =
            tr("%1 frames\nWeight: %2 - %3").arg(count).arg(rangeStart, 0, 'f', 2).arg(rangeEnd, 0, 'f', 2);

        const QRect textRect = p.fontMetrics().boundingRect(QRect(0, 0, 200, 100), Qt::AlignLeft, text);
        const QRect bgRect = textRect.adjusted(-4, -2, 4, 2);

        int tx = m_hoverPos.x() + 12;
        int ty = m_hoverPos.y() + 12;
        if (tx + bgRect.width() > width())
            tx = m_hoverPos.x() - bgRect.width() - 4;
        if (ty + bgRect.height() > height())
            ty = m_hoverPos.y() - bgRect.height() - 4;

        p.translate(tx, ty);
        p.setPen(palette().text().color());
        p.setBrush(palette().window());
        p.drawRect(bgRect);
        p.drawText(textRect, Qt::AlignLeft, text);
        p.resetTransform();
    }

    void updateToolTip()
    {
        if (m_weights.isEmpty())
        {
            setToolTip(tr("No preview weights are available."));
            return;
        }

        int count = 0;
        for (double w : m_weights)
        {
            if (std::isfinite(w))
                ++count;
        }

        if (count <= 0)
        {
            setToolTip(tr("No preview weights are available."));
            return;
        }

        const QString semantics = m_meanNormalizedWeights
                                      ? tr("Preview weight distribution (1.0 = average frame contribution)")
                                      : tr("Preview weight distribution (0..1)");
        setToolTip(tr("%1\nFrames: %2\nHover over bars for details.").arg(semantics).arg(count));
    }

    std::array<int, kHistogramBins> m_bins{};
    QVector<double> m_weights{};
    double m_selectedWeight = std::numeric_limits<double>::quiet_NaN();
    double m_minFloor = 0.0;
    bool m_meanNormalizedWeights = false;
    double m_plotMin = 0.0;
    double m_plotMax = 1.0;

    bool m_hovering = false;
    QPoint m_hoverPos;
    int m_hoverBin = -1;
};
