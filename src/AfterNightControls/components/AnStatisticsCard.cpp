// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnStatisticsCard.h"

#include "components/AnIconButton.h"
#include "components/AnReadoutGrid.h"
#include "components/AnSegmentedControl.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

W_OBJECT_IMPL(an::ui::AnStatisticsCard)

namespace an::ui
{

/// The colour table: a header row of the channels' dots and letters, then a row per statistic with its name and the
/// channels' figures in the monospace, right-aligned in their columns. Values are drawn at 55 % while stale.
class StatisticsTable final : public QWidget
{
public:
    explicit StatisticsTable(QWidget* parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    /// The table's figures: a row per statistic, a text per channel.
    void setRows(std::vector<std::array<QString, 3>> rows)
    {
        m_rows = std::move(rows);
        QStringList described;
        for (std::size_t row = 0u; row < m_rows.size() && row < kReadoutStatistics.size(); ++row)
        {
            described.append(QStringLiteral("%1 R %2 G %3 B %4")
                                 .arg(readoutStatisticName(kReadoutStatistics[row]), m_rows[row][0], m_rows[row][1],
                                      m_rows[row][2]));
        }
        setAccessibleDescription(described.join(QStringLiteral(", ")));
        update();
    }

    void setStale(const bool stale)
    {
        m_stale = stale;
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const int row = tokens().metrics.rowKeyValue;
        return QSize(row * 8, row * static_cast<int>(1u + kReadoutStatistics.size()));
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }

    /// The rectangle of a row - 0 the header, 1 to 6 the statistics - and of a column - 0 the names, 1 to 3 the
    /// channels.
    [[nodiscard]] QRect cellRect(const int row, const int column) const
    {
        const int rowHeight = tokens().metrics.rowKeyValue;
        const int nameWidth = width() / 4;
        const int channelWidth = (width() - nameWidth) / 3;
        const int left = column == 0 ? 0 : nameWidth + (column - 1) * channelWidth;
        return QRect(left, row * rowHeight, column == 0 ? nameWidth : channelWidth, rowHeight);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QFont caps = parts::capsFont(t.typography.body, t.metrics.fontCaption);
        const QFont name = parts::sizedFont(t.typography.body, t.metrics.fontSmall);
        const QFont value = parts::sizedFont(t.typography.monospace, t.metrics.fontSmall);
        const QFontMetricsF valueMetrics(value);
        const std::array<QColor, 3> dots{t.colors.channelRed, t.colors.channelGreen, t.colors.channelBlue};
        const std::array<QString, 3> letters{QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};

        for (int column = 1; column <= 3; ++column)
        {
            const QRect cell = cellRect(0, column);
            const int letterWidth = parts::textWidth(caps, letters[static_cast<std::size_t>(column - 1)]);
            const int right = cell.right();
            const int letterLeft = right - letterWidth;
            const qreal dotLeft = letterLeft - parts::kReadoutDotGap - parts::kReadoutDot;
            painter.setPen(Qt::NoPen);
            painter.setBrush(dots[static_cast<std::size_t>(column - 1)]);
            painter.drawEllipse(QRectF(dotLeft, cell.center().y() - parts::kReadoutDot / 2.0 + 0.5, parts::kReadoutDot,
                                       parts::kReadoutDot));
            painter.setFont(caps);
            painter.setPen(t.colors.textSecondary);
            painter.drawText(QRect(letterLeft, cell.top(), letterWidth + 1, cell.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, letters[static_cast<std::size_t>(column - 1)]);
        }

        for (std::size_t row = 0u; row < m_rows.size() && row < kReadoutStatistics.size(); ++row)
        {
            const int line = static_cast<int>(row) + 1;
            painter.setOpacity(1.0);
            painter.setFont(name);
            painter.setPen(t.colors.textSecondary);
            painter.drawText(cellRect(line, 0), Qt::AlignLeft | Qt::AlignVCenter,
                             readoutStatisticName(kReadoutStatistics[row]));
            painter.setOpacity(m_stale ? parts::kReadoutStaleOpacity : 1.0);
            painter.setFont(value);
            painter.setPen(t.colors.textPrimary);
            for (int column = 1; column <= 3; ++column)
            {
                const QRect cell = cellRect(line, column);
                painter.drawText(cell, Qt::AlignRight | Qt::AlignVCenter,
                                 valueMetrics.elidedText(m_rows[row][static_cast<std::size_t>(column - 1)],
                                                         Qt::ElideRight, cell.width()));
            }
        }
    }

private:
    std::vector<std::array<QString, 3>> m_rows;
    bool m_stale = false;
};

namespace
{

constexpr std::array<StatisticsScale, 3> kScales{StatisticsScale::Normalized, StatisticsScale::Bits8,
                                                 StatisticsScale::Bits16};

} // namespace

AnStatisticsCard::AnStatisticsCard(QWidget* parent) : AnReadoutCard(tr("Statistics"), parent)
{
    m_range = new AnSegmentedControl(QStringList{tr("Normalized"), tr("8-bit"), tr("16-bit")});
    m_range->setControlHeight(AnSegmentedControl::ControlHeight::ExtraSmall);
    m_range->setAccessibleName(tr("Range"));
    m_range->setCurrentIndex(0);
    addHeaderWidget(m_range);
    m_window = addHeaderTool(QStringLiteral("open_in_new"),
                             tr("Open Image Statistics: 13 statistics per channel (Ctrl+Shift+T)"));

    m_grey = new AnReadoutGrid(3, body());
    m_table = new StatisticsTable(body());
    bodyLayout()->addWidget(m_grey);
    bodyLayout()->addWidget(m_table);
    m_table->hide();
    refill();

    QObject::connect(m_range, &AnSegmentedControl::currentIndexChanged, this, [this](const int index) {
        if (index < 0 || index >= static_cast<int>(kScales.size()))
        {
            return;
        }
        if (m_scale != kScales[static_cast<std::size_t>(index)])
        {
            m_scale = kScales[static_cast<std::size_t>(index)];
            refill();
            scaleChanged();
        }
    });
    QObject::connect(m_window, &QAbstractButton::clicked, this, [this]() { statisticsWindowRequested(); });
}

AnStatisticsCard::~AnStatisticsCard() = default;

void AnStatisticsCard::setReport(std::optional<ImageStatisticsReport> report)
{
    m_report = std::move(report);
    refill();
}

const std::optional<ImageStatisticsReport>& AnStatisticsCard::report() const noexcept
{
    return m_report;
}

StatisticsScale AnStatisticsCard::scale() const noexcept
{
    return m_scale;
}

void AnStatisticsCard::setScale(const StatisticsScale scale)
{
    for (std::size_t index = 0u; index < kScales.size(); ++index)
    {
        if (kScales[index] == scale)
        {
            m_range->setCurrentIndex(static_cast<int>(index));
        }
    }
}

double AnStatisticsCard::figure(const ReadoutStatistic statistic, const std::size_t channel) const
{
    if (!m_report.has_value() || channel >= readoutChannelCount(*m_report))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return readoutFigure(m_report->channels[channel], statistic, m_scale);
}

QString AnStatisticsCard::figureText(const ReadoutStatistic statistic, const std::size_t channel) const
{
    return formatReadoutFigure(figure(statistic, channel), m_scale);
}

bool AnStatisticsCard::showsTable() const
{
    return m_report.has_value() && readoutChannelCount(*m_report) == 3u;
}

AnSegmentedControl* AnStatisticsCard::rangePicker() const noexcept
{
    return m_range;
}

AnIconButton* AnStatisticsCard::windowButton() const noexcept
{
    return m_window;
}

AnReadoutGrid* AnStatisticsCard::greyReadouts() const noexcept
{
    return m_grey;
}

QWidget* AnStatisticsCard::colourTable() const noexcept
{
    return m_table;
}

void AnStatisticsCard::staleChanged()
{
    m_grey->setStale(isStale());
    m_table->setStale(isStale());
}

void AnStatisticsCard::refill()
{
    const bool table = showsTable();
    if (table)
    {
        std::vector<std::array<QString, 3>> rows;
        for (const ReadoutStatistic statistic : kReadoutStatistics)
        {
            rows.push_back({figureText(statistic, 0u), figureText(statistic, 1u), figureText(statistic, 2u)});
        }
        m_table->setRows(std::move(rows));
    }
    else
    {
        std::vector<ReadoutCell> cells;
        for (const ReadoutStatistic statistic : kReadoutStatistics)
        {
            cells.push_back(
                ReadoutCell{.label = readoutStatisticName(statistic),
                            .value = m_report.has_value() ? figureText(statistic, 0u) : QStringLiteral("-")});
        }
        m_grey->setCells(std::move(cells));
    }
    m_grey->setVisible(!table);
    m_table->setVisible(table);
}

} // namespace an::ui
