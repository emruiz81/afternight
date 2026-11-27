// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameStatisticsDashboard.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "theme/AnDesignTokens.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QToolTip>

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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

    /// The figures' labels and values, in the order they are drawn.
    [[nodiscard]] std::vector<std::pair<QString, QString>> figures() const
    {
        std::vector<std::pair<QString, QString>> lines;
        lines.emplace_back(q->tr("Total:"), QString::number(stats.totalFrames));
        lines.emplace_back(q->tr("Included:"), QString::number(stats.includedFrames));
        lines.emplace_back(q->tr("Excluded:"), QString::number(stats.excludedFrames));
        lines.emplace_back(q->tr("Groups:"), QString::number(stats.groupCount));
        if (frameType != FrameType::Light)
        {
            lines.emplace_back(q->tr("Masters:"), QString::number(stats.masterCount));
        }
        // The total exposure of the lights.
        if (frameType == FrameType::Light && stats.totalExposureSec > 0)
        {
            lines.emplace_back(q->tr("Exposure:"), formatDuration(stats.totalExposureSec));
        }
        // The total size of the files there are.
        if (stats.totalFileSizeBytes > 0)
        {
            lines.emplace_back(q->tr("Size:"), formatFileSize(stats.totalFileSizeBytes));
        }
        return lines;
    }

    /// Where the parts are drawn at a size: the chart, the place of the figures, and each figure's label and value.
    struct Layout
    {
        bool horizontal = false;
        QRect chart;
        QRect stats;
        std::vector<FigureLine> lines;

        /// The top of the legend, under the figures.
        int legendTop = 0;
    };

    /// What the figures take in the dashboard's font: the widest label, the widest value, and half the legend - a box,
    /// its gap and the longer of its two words.
    struct Measures
    {
        int label = 0;
        int value = 0;
        int legendHalf = 0;
    };

    [[nodiscard]] Measures measures() const
    {
        const QFontMetricsF metrics(q->font());
        Measures measured;
        for (const auto& [label, value] : figures())
        {
            measured.label = (std::max)(measured.label, static_cast<int>(std::ceil(metrics.horizontalAdvance(label))));
            measured.value = (std::max)(measured.value, static_cast<int>(std::ceil(metrics.horizontalAdvance(value))));
        }
        const qreal words =
            (std::max)(metrics.horizontalAdvance(q->tr("Included")), metrics.horizontalAdvance(q->tr("Excluded")));
        measured.legendHalf = kLegendBox + kLegendGap + static_cast<int>(std::ceil(words));
        return measured;
    }

    [[nodiscard]] Layout layoutAt(const QSize size) const
    {
        const int w = size.width();
        const int h = size.height();
        const Measures measured = measures();
        Layout layout;

        const int availableH = (std::max)(0, h - kContentTop - 8);
        const int chartSize = (std::max)(0, (std::min)(kChartSize, availableH));
        const int chartY = kContentTop;

        // The pie at the left and the figures at its right where they fit there whole: the labels at their usual width
        // after the usual gap while there is room, else the labels as wide as the widest and the gap narrowed, down to
        // its least. Narrower still, the figures go under the pie.
        const int chartRight = kMarginX + chartSize - 1;
        const int beside = w - kMarginX - chartRight;
        const auto figuresNeed = [&measured](const int labelWidth) {
            return (std::max)(labelWidth + kValueGap + measured.value, 2 * measured.legendHalf);
        };
        int labelWidth = (std::max)(kLabelWidth, measured.label);
        int gap = kGapX;
        if (beside - gap < figuresNeed(labelWidth))
        {
            labelWidth = measured.label;
            gap = (std::clamp)(beside - figuresNeed(labelWidth), kLeastGapX, kGapX);
        }
        layout.horizontal = beside - gap >= figuresNeed(labelWidth);
        if (layout.horizontal)
        {
            layout.chart = QRect(kMarginX, chartY, chartSize, chartSize);
            const int statsX = layout.chart.right() + gap;
            layout.stats =
                QRect(statsX, kContentTop, (std::max)(0, w - kMarginX - statsX), (std::max)(0, h - kContentTop));
        }
        else
        {
            const int chartX = (w - chartSize) / 2;
            layout.chart = QRect(chartX, chartY, chartSize, chartSize);
            const int statsY = chartY + chartSize + 22;
            layout.stats = QRect(0, statsY, w, (std::max)(0, h - statsY));
            labelWidth = (std::max)(kLabelWidth, measured.label);
            if (labelWidth + kValueGap + measured.value > layout.stats.width())
            {
                labelWidth = measured.label;
            }
        }

        int currentY = layout.stats.y();
        for (const auto& [label, value] : layout.stats.width() > 0 ? figures() : decltype(figures()){})
        {
            // Keep values close to labels (no large "tab stop" feel).
            const QRect labelRect(layout.stats.x(), currentY, (std::min)(labelWidth, layout.stats.width()),
                                  kLineHeight);
            const QRect valueRect(labelRect.right() + kValueGap, currentY,
                                  (std::max)(0, layout.stats.right() - (labelRect.right() + kValueGap) + 1),
                                  kLineHeight);
            layout.lines.push_back(FigureLine{label, value, labelRect, valueRect});
            currentY += kLineHeight;
        }
        layout.legendTop = currentY + 6;
        return layout;
    }

    /// The height the dashboard needs at a width: the pie beside the figures and the legend, or over them.
    [[nodiscard]] int heightAt(const int width) const
    {
        const int lines = static_cast<int>(figures().size());
        const int figuresHeight = (lines * kLineHeight) + 6 + kLineHeight;
        if (stats.totalFrames == 0 || layoutAt(QSize(width, kContentTop + kChartSize + 8)).horizontal)
        {
            return kContentTop + (std::max)(kChartSize, figuresHeight) + 8;
        }
        return kContentTop + kChartSize + 22 + figuresHeight + 8;
    }

    /// The least width the figures take under the pie, and the legend's two halves across the dashboard.
    [[nodiscard]] int stackedLeastWidth() const
    {
        const Measures measured = measures();
        return (
            std::max)({kChartSize, measured.label + kValueGap + measured.value, 2 * (kMarginX + measured.legendHalf)});
    }

    static constexpr int kContentTop = 28;
    static constexpr int kMarginX = 12;
    static constexpr int kChartSize = 90;

    /// The gap after the pie where there is room, and the least it narrows to before the figures go under the pie.
    static constexpr int kGapX = 85;
    static constexpr int kLeastGapX = 16;

    /// A label's usual width, and the gap between a label and its value.
    static constexpr int kLabelWidth = 86;
    static constexpr int kValueGap = 10;
    static constexpr int kLineHeight = 18;

    /// A legend's box and the gap between it and its word.
    static constexpr int kLegendBox = 10;
    static constexpr int kLegendGap = 6;

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

std::vector<FrameStatisticsDashboard::FigureLine> FrameStatisticsDashboard::figureLines() const
{
    if (m_impl->stats.totalFrames == 0)
    {
        return {};
    }
    return m_impl->layoutAt(size()).lines;
}

void FrameStatisticsDashboard::refresh()
{
    m_impl->calculateStats();
    keepHeightForWidth();
    update();
}

int FrameStatisticsDashboard::heightForWidth(const int width) const
{
    return m_impl->heightAt(width);
}

QSize FrameStatisticsDashboard::sizeHint() const
{
    // Designed for the right-side dashboard column: prefer a compact height,
    // but ensure enough room for the stats + legend so content doesn't get clipped.
    constexpr int kWidth = 260;
    return QSize(kWidth, m_impl->heightAt(width() > 0 ? width() : kWidth));
}

QSize FrameStatisticsDashboard::minimumSizeHint() const
{
    // Allow it to shrink, but keep it usable: never narrower than the figures under the pie take.
    return QSize((std::max)(220, m_impl->stackedLeastWidth()), 140);
}

void FrameStatisticsDashboard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    keepHeightForWidth();
}

void FrameStatisticsDashboard::keepHeightForWidth()
{
    // The height follows the width - the figures go under the pie where they do not fit beside it - and is held as the
    // dashboard's least, so the column it stands in gives it that height without asking every part for its height at
    // its width.
    const int height = m_impl->heightAt(width());
    if (minimumHeight() != height)
    {
        setMinimumHeight(height);
    }
}

void FrameStatisticsDashboard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const int w = width();

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

    constexpr int kMarginX = Impl::kMarginX;
    constexpr int kLineHeight = Impl::kLineHeight;
    const Impl::Layout layout = m_impl->layoutAt(size());
    const bool useHorizontalLayout = layout.horizontal;

    // --- Donut Chart ---
    const QRect chartRect = layout.chart;
    const int chartSize = chartRect.width();

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
    const QRect statsRect = layout.stats;

    p.setFont(font());
    if (statsRect.width() > 0)
    {
        p.setPen(palette().text().color());
        for (const FigureLine& line : layout.lines)
        {
            p.drawText(line.labelRect, Qt::AlignLeft | Qt::AlignVCenter, line.label);
            p.drawText(line.valueRect, Qt::AlignLeft | Qt::AlignVCenter, line.value);
        }
    }

    // --- Legend ---
    const int currentY = layout.legendTop;
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
        const Impl::Layout layout = m_impl->layoutAt(size());
        const QPoint pos = helpEvent->pos();
        const bool overPie = layout.chart.contains(pos);
        const bool overStats = layout.stats.contains(pos);

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
