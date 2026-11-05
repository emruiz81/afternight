// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameStatisticsDashboard.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "theme/AnDesignTokens.h"

#include <QFileInfo>
#include <QHelpEvent>
#include <QPainter>
#include <QPalette>
#include <QToolTip>

#include <QStringList>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::FrameStatisticsDashboard)

namespace ImageIntegration
{

namespace
{

[[nodiscard]] QString joinNonEmpty(const QStringList& parts, const QString& sep)
{
    QStringList out;
    out.reserve(parts.size());
    for (const auto& p : parts)
    {
        const QString trimmed = p.trimmed();
        if (!trimmed.isEmpty())
        {
            out << trimmed;
        }
    }
    return out.join(sep);
}

QString formatDuration(double totalSeconds)
{
    if (totalSeconds <= 0.0)
    {
        return QStringLiteral("0s");
    }

    const int hours = static_cast<int>(totalSeconds / 3600.0);
    const int minutes = static_cast<int>(std::fmod(totalSeconds, 3600.0) / 60.0);
    const int seconds = static_cast<int>(std::fmod(totalSeconds, 60.0));

    if (hours > 0)
    {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0)
    {
        return QStringLiteral("%1m %2s").arg(minutes).arg(seconds);
    }
    return QStringLiteral("%1s").arg(seconds);
}

QString formatFileSize(qint64 bytes)
{
    if (bytes <= 0)
    {
        return QStringLiteral("0 B");
    }

    constexpr double kKB = 1024.0;
    constexpr double kMB = kKB * 1024.0;
    constexpr double kGB = kMB * 1024.0;

    if (bytes >= kGB)
    {
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / kGB, 0, 'f', 1);
    }
    if (bytes >= kMB)
    {
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / kMB, 0, 'f', 1);
    }
    if (bytes >= kKB)
    {
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / kKB, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

} // anonymous namespace

struct FrameStats
{
    int totalFrames = 0;
    int includedFrames = 0;
    int excludedFrames = 0;
    int groupCount = 0;
    int masterCount = 0;
    double totalExposureSec = 0.0;
    qint64 totalFileSizeBytes = 0;
};

class FrameStatisticsDashboard::Impl
{
public:
    FrameStatisticsDashboard* q;
    IntegrationDataModel* model;
    FrameType frameType;
    FrameStats stats;

    [[nodiscard]] QColor includedColor() const
    {
        return an::ui::tokens().colors.statusSuccess;
    }

    [[nodiscard]] QColor excludedColor() const
    {
        return an::ui::tokens().colors.textTertiary;
    }

    explicit Impl(FrameStatisticsDashboard* parent, FrameType type, IntegrationDataModel* m)
        : q(parent), model(m), frameType(type)
    {
    }

    void calculateStats()
    {
        stats = FrameStats();
        if (!model)
        {
            return;
        }

        const auto& collection = model->frames(frameType);
        const auto& frames = collection.frames();

        stats.totalFrames = static_cast<int>(frames.size());
        stats.masterCount = model->masterCount(frameType);

        for (const auto& frame : frames)
        {
            if (frame.excluded)
            {
                ++stats.excludedFrames;
            }
            else
            {
                ++stats.includedFrames;
                stats.totalExposureSec += frame.metadata.exposureSeconds();
            }
            const QFileInfo fileInfo(frame.id.canonicalPath());
            if (fileInfo.exists())
            {
                stats.totalFileSizeBytes += fileInfo.size();
            }
        }

        switch (frameType)
        {
        case FrameType::Bias:
            stats.groupCount = static_cast<int>(model->biasGroups().size());
            break;
        case FrameType::Dark:
            stats.groupCount = static_cast<int>(model->darkGroups().size());
            break;
        case FrameType::Flat:
            stats.groupCount = static_cast<int>(model->flatGroups().size());
            break;
        case FrameType::Light:
            stats.groupCount = static_cast<int>(model->lightGroups().size());
            break;
        }
    }

    QString frameTypeName() const
    {
        switch (frameType)
        {
        case FrameType::Bias:
            return QObject::tr("Bias");
        case FrameType::Dark:
            return QObject::tr("Darks");
        case FrameType::Flat:
            return QObject::tr("Flats");
        case FrameType::Light:
            return QObject::tr("Lights");
        }
        return QObject::tr("Frames");
    }

    [[nodiscard]] QString summaryTooltipHtml() const
    {
        QString tooltip = q->tr("<b>%1 Statistics</b><br/>"
                                "Total: %2<br/>"
                                "<span style='color:%3'>●</span> Included: %4<br/>"
                                "<span style='color:%5'>●</span> Excluded: %6<br/>"
                                "Groups: %7")
                              .arg(frameTypeName())
                              .arg(stats.totalFrames)
                              .arg(includedColor().name())
                              .arg(stats.includedFrames)
                              .arg(excludedColor().name())
                              .arg(stats.excludedFrames)
                              .arg(stats.groupCount);

        if (frameType != FrameType::Light)
        {
            tooltip += q->tr("<br/>Masters: %1").arg(stats.masterCount);
        }
        if (frameType == FrameType::Light && stats.totalExposureSec > 0)
        {
            tooltip += q->tr("<br/>Total Exposure: %1").arg(formatDuration(stats.totalExposureSec));
        }
        if (stats.totalFileSizeBytes > 0)
        {
            tooltip += q->tr("<br/>Total Size: %1").arg(formatFileSize(stats.totalFileSizeBytes));
        }
        return tooltip;
    }

    [[nodiscard]] QString groupsTooltipHtml() const
    {
        if (!model)
        {
            return QString();
        }

        const GroupedFrameView* groups = nullptr;
        switch (frameType)
        {
        case FrameType::Bias:
            groups = &model->biasGroups();
            break;
        case FrameType::Dark:
            groups = &model->darkGroups();
            break;
        case FrameType::Flat:
            groups = &model->flatGroups();
            break;
        case FrameType::Light:
            groups = &model->lightGroups();
            break;
        }

        if (!groups)
        {
            return QString();
        }

        const auto& options = model->groupingOptions(frameType);

        QString html;
        html += q->tr("<b>%1 Group Report</b><br/>").arg(frameTypeName());
        html += q->tr("Groups: %1<br/>").arg(static_cast<int>(groups->size()));
        html += q->tr("<span style='color:%1'>●</span> Included frames: %2<br/>")
                    .arg(includedColor().name())
                    .arg(stats.includedFrames);
        html += q->tr("<span style='color:%1'>●</span> Excluded frames: %2<br/>")
                    .arg(excludedColor().name())
                    .arg(stats.excludedFrames);

        html += q->tr("<br/><b>Details</b><br/>");

        int groupIndex = 1;
        for (const auto& group : *groups)
        {
            const int total = static_cast<int>(group.frames.size());
            int excluded = 0;
            for (const auto& frame : group.frames)
            {
                if (frame.excluded)
                {
                    ++excluded;
                }
            }
            const int included = total - excluded;

            QString groupLabel = group.key.toDisplayString(options, frameType);
            if (groupLabel.trimmed().isEmpty())
            {
                groupLabel = group.key.toString();
            }

            QString details;
            if (!group.frames.empty())
            {
                const auto& rep = group.frames.front();
                const auto& meta = rep.metadata;

                QStringList parts;

                // For humans: put the most recognizable fields first.
                if (frameType == FrameType::Flat || frameType == FrameType::Light)
                {
                    const QString filter = meta.filter();
                    if (!filter.trimmed().isEmpty())
                    {
                        parts << q->tr("Filter: %1").arg(filter);
                    }

                    const QString object = rep.effectiveObjectName();
                    if (!object.trimmed().isEmpty())
                    {
                        parts << q->tr("Object: %1").arg(object);
                    }

                    const QString session = rep.effectiveSession();
                    if (!session.trimmed().isEmpty())
                    {
                        parts << q->tr("Session: %1").arg(session);
                    }
                }

                if (frameType == FrameType::Dark || frameType == FrameType::Light)
                {
                    const double exp = meta.exposureSeconds();
                    if (exp > 0.0)
                    {
                        parts << q->tr("Exposure: %1s").arg(QString::number(exp, 'f', 3));
                    }
                }

                const QString res = meta.resolutionString();
                if (!res.trimmed().isEmpty())
                {
                    parts << q->tr("Resolution: %1").arg(res);
                }

                if (meta.isoGain().has_value())
                {
                    parts << q->tr("Gain/ISO: %1").arg(meta.isoGain().value());
                }
                if (meta.offset().has_value())
                {
                    parts << q->tr("Offset: %1").arg(meta.offset().value());
                }
                if (meta.temperatureC().has_value())
                {
                    parts << q->tr("Temp: %1C").arg(QString::number(meta.temperatureC().value(), 'f', 1));
                }

                if (frameType == FrameType::Bias)
                {
                    const QString readout = meta.readoutMode();
                    if (!readout.trimmed().isEmpty())
                    {
                        parts << q->tr("Readout: %1").arg(readout);
                    }
                }

                if (!rep.customTag.trimmed().isEmpty())
                {
                    parts << q->tr("Tag: %1").arg(rep.customTag);
                }

                details = joinNonEmpty(parts, q->tr("; "));
            }

            const QString lineHeader =
                q->tr("%1) %2 frames (%3 incl, %4 excl)").arg(groupIndex).arg(total).arg(included).arg(excluded);

            html += q->tr("%1 — <b>%2</b>").arg(lineHeader.toHtmlEscaped(), groupLabel.toHtmlEscaped());
            if (!details.isEmpty())
            {
                html += q->tr("<br/>&nbsp;&nbsp;%1").arg(details.toHtmlEscaped());
            }
            html += QStringLiteral("<br/>");

            ++groupIndex;
        }

        return html;
    }
};

FrameStatisticsDashboard::FrameStatisticsDashboard(FrameType frameType, IntegrationDataModel* model, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, frameType, model))
{
    setObjectName(QStringLiteral("frameStatisticsDashboard"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

FrameStatisticsDashboard::~FrameStatisticsDashboard() = default;

void FrameStatisticsDashboard::setFrameType(FrameType type)
{
    m_impl->frameType = type;
    refresh();
}

FrameType FrameStatisticsDashboard::frameType() const
{
    return m_impl->frameType;
}

void FrameStatisticsDashboard::refresh()
{
    m_impl->calculateStats();
    update();
}

QSize FrameStatisticsDashboard::sizeHint() const
{
    // Designed for the right-side dashboard column: prefer a compact height,
    // but ensure enough room for the stats + legend so content doesn't get clipped.
    int lineCount = 4; // Total, Included, Excluded, Groups
    if (m_impl->frameType != FrameType::Light)
    {
        ++lineCount; // Masters
    }
    if (m_impl->frameType == FrameType::Light && m_impl->stats.totalExposureSec > 0)
    {
        ++lineCount; // Exposure
    }
    if (m_impl->stats.totalFileSizeBytes > 0)
    {
        ++lineCount; // Size
    }

    constexpr int kLineHeight = 18;
    constexpr int kContentTop = 28;
    const int statsAndLegendH = (lineCount * kLineHeight) + 6 + kLineHeight;
    const int contentH = (std::max)(90, statsAndLegendH);
    const int totalH = kContentTop + contentH + 8;

    return QSize(260, totalH);
}

QSize FrameStatisticsDashboard::minimumSizeHint() const
{
    // Allow it to shrink, but keep it usable.
    return QSize(220, 140);
}

void FrameStatisticsDashboard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const int w = width();
    const int h = height();

    // --- Title ---
    p.setPen(palette().text().color());
    QFont titleFont = font();
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRect(0, 4, w, 20), Qt::AlignHCenter, tr("Frame Statistics"));

    if (m_impl->stats.totalFrames == 0)
    {
        p.setPen(palette().placeholderText().color());
        p.setFont(font());
        p.drawText(rect().adjusted(0, 30, 0, 0), Qt::AlignCenter,
                   tr("No %1 loaded.").arg(m_impl->frameTypeName().toLower()));
        return;
    }

    const int contentTop = 28;
    constexpr int kMarginX = 12;
    constexpr int kGapX = 85;
    constexpr int kLineHeight = 18;

    // Prefer a horizontal layout (pie left, stats right) when there's enough width.
    // Fall back to the stacked layout on narrow widths.
    const bool useHorizontalLayout = (w >= 300);

    // --- Donut Chart ---
    const int maxChartSize = 90;
    const int availableH = (std::max)(0, h - contentTop - 8);
    const int chartSize = (std::max)(0, (std::min)(maxChartSize, availableH));
    const int chartY = contentTop;

    QRect chartRect;
    if (useHorizontalLayout)
    {
        chartRect = QRect(kMarginX, chartY, chartSize, chartSize);
    }
    else
    {
        const int chartX = (w - chartSize) / 2;
        chartRect = QRect(chartX, chartY, chartSize, chartSize);
    }

    // Draw included segment (green)
    if (m_impl->stats.includedFrames > 0)
    {
        const int spanAngle = (360 * 16 * m_impl->stats.includedFrames) / m_impl->stats.totalFrames;
        p.setBrush(m_impl->includedColor());
        p.setPen(Qt::NoPen);
        p.drawPie(chartRect, 90 * 16, spanAngle);
    }

    // Draw excluded segment (gray)
    if (m_impl->stats.excludedFrames > 0)
    {
        const int startAngle = 90 * 16 + (360 * 16 * m_impl->stats.includedFrames) / m_impl->stats.totalFrames;
        const int spanAngle = (360 * 16 * m_impl->stats.excludedFrames) / m_impl->stats.totalFrames;
        p.setBrush(m_impl->excludedColor());
        p.setPen(Qt::NoPen);
        p.drawPie(chartRect, startAngle, spanAngle);
    }

    // Draw percentage in center (no inner fill circle)
    const int percent =
        m_impl->stats.totalFrames > 0 ? (m_impl->stats.includedFrames * 100 / m_impl->stats.totalFrames) : 0;
    QFont percentFont = an::ui::tokens().typography.heading2;
    p.setFont(percentFont);
    p.setPen(palette().text().color());
    const QRect percentRect = chartRect.adjusted(chartSize / 4, chartSize / 4, -chartSize / 4, -chartSize / 4);
    p.drawText(percentRect, Qt::AlignCenter, tr("%1%").arg(percent));

    // --- Statistics List ---
    QRect statsRect;
    if (useHorizontalLayout)
    {
        const int statsX = chartRect.right() + kGapX;
        statsRect = QRect(statsX, contentTop, (std::max)(0, w - kMarginX - statsX), (std::max)(0, h - contentTop));
    }
    else
    {
        const int statsY = chartY + chartSize + 22;
        statsRect = QRect(0, statsY, w, (std::max)(0, h - statsY));
    }

    int currentY = statsRect.y();

    p.setFont(font());

    auto drawStatLine = [&](const QString& label, const QString& value) {
        if (statsRect.width() <= 0)
        {
            return;
        }

        p.setPen(palette().text().color());

        // Keep values close to labels (no large "tab stop" feel).
        const int desiredLabelWidth = 86;
        const int labelWidth = (std::min)(desiredLabelWidth, statsRect.width());
        const QRect labelRect(statsRect.x(), currentY, labelWidth, kLineHeight);
        const QRect valueRect(labelRect.right() + 10, currentY,
                              (std::max)(0, statsRect.right() - (labelRect.right() + 10) + 1), kLineHeight);

        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, label);
        p.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, value);

        currentY += kLineHeight;
    };

    drawStatLine(tr("Total:"), QString::number(m_impl->stats.totalFrames));
    drawStatLine(tr("Included:"), QString::number(m_impl->stats.includedFrames));
    drawStatLine(tr("Excluded:"), QString::number(m_impl->stats.excludedFrames));
    drawStatLine(tr("Groups:"), QString::number(m_impl->stats.groupCount));

    if (m_impl->frameType != FrameType::Light)
    {
        drawStatLine(tr("Masters:"), QString::number(m_impl->stats.masterCount));
    }

    // Show total exposure for lights
    if (m_impl->frameType == FrameType::Light && m_impl->stats.totalExposureSec > 0)
    {
        drawStatLine(tr("Exposure:"), formatDuration(m_impl->stats.totalExposureSec));
    }

    // Show total file size if we have data
    if (m_impl->stats.totalFileSizeBytes > 0)
    {
        drawStatLine(tr("Size:"), formatFileSize(m_impl->stats.totalFileSizeBytes));
    }

    // --- Legend ---
    currentY += 6;
    const int boxSize = 10;

    if (useHorizontalLayout)
    {
        const int legendX = statsRect.x();
        const int legendW = statsRect.width();

        const int leftX = legendX;
        const int rightX = legendX + (legendW / 2);

        p.setBrush(m_impl->includedColor());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(leftX, currentY + 4, boxSize, boxSize, an::ui::tokens().radius.sm,
                          an::ui::tokens().radius.sm);
        p.setPen(palette().text().color());
        p.drawText(leftX + boxSize + 6, currentY, (std::max)(0, (legendW / 2) - boxSize - 6), kLineHeight,
                   Qt::AlignLeft | Qt::AlignVCenter, tr("Included"));

        p.setBrush(m_impl->excludedColor());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rightX, currentY + 4, boxSize, boxSize, an::ui::tokens().radius.sm,
                          an::ui::tokens().radius.sm);
        p.setPen(palette().text().color());
        p.drawText(rightX + boxSize + 6, currentY, (std::max)(0, legendX + legendW - (rightX + boxSize + 6)),
                   kLineHeight, Qt::AlignLeft | Qt::AlignVCenter, tr("Excluded"));
    }
    else
    {
        const int legendX = kMarginX;

        p.setBrush(m_impl->includedColor());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(legendX, currentY + 4, boxSize, boxSize, an::ui::tokens().radius.sm,
                          an::ui::tokens().radius.sm);
        p.setPen(palette().text().color());
        p.drawText(legendX + boxSize + 6, currentY, w / 2 - legendX - boxSize - 6, kLineHeight,
                   Qt::AlignLeft | Qt::AlignVCenter, tr("Included"));

        p.setBrush(m_impl->excludedColor());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(w / 2, currentY + 4, boxSize, boxSize, an::ui::tokens().radius.sm,
                          an::ui::tokens().radius.sm);
        p.setPen(palette().text().color());
        p.drawText(w / 2 + boxSize + 6, currentY, w - w / 2 - boxSize - 12, kLineHeight,
                   Qt::AlignLeft | Qt::AlignVCenter, tr("Excluded"));
    }
}

bool FrameStatisticsDashboard::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* helpEvent = static_cast<QHelpEvent*>(event);

        // Match paint geometry so tooltip changes based on hover region.
        const int w = width();
        const int h = height();
        const int contentTop = 28;
        constexpr int kMarginX = 12;
        constexpr int kGapX = 85;

        const bool useHorizontalLayout = (w >= 300);
        const int maxChartSize = 90;
        const int availableH = (std::max)(0, h - contentTop - 8);
        const int chartSize = (std::max)(0, (std::min)(maxChartSize, availableH));
        const int chartY = contentTop;

        QRect chartRect;
        if (useHorizontalLayout)
        {
            chartRect = QRect(kMarginX, chartY, chartSize, chartSize);
        }
        else
        {
            const int chartX = (w - chartSize) / 2;
            chartRect = QRect(chartX, chartY, chartSize, chartSize);
        }

        QRect statsRect;
        if (useHorizontalLayout)
        {
            const int statsX = chartRect.right() + kGapX;
            statsRect = QRect(statsX, contentTop, (std::max)(0, w - kMarginX - statsX), (std::max)(0, h - contentTop));
        }
        else
        {
            const int statsY = chartY + chartSize + 22;
            statsRect = QRect(0, statsY, w, (std::max)(0, h - statsY));
        }

        const QPoint pos = helpEvent->pos();
        const bool overPie = chartRect.contains(pos);
        const bool overStats = statsRect.contains(pos);

        const QString tooltip = (overPie || !overStats) ? m_impl->summaryTooltipHtml() : m_impl->groupsTooltipHtml();

        if (!tooltip.isEmpty())
        {
            QToolTip::showText(helpEvent->globalPos(), tooltip);
        }
        return true;
    }
    return QWidget::event(event);
}

} // namespace ImageIntegration
