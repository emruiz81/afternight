// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnCompactReadout.h"

#include "components/AnIconButton.h"
#include "components/AnReadoutGrid.h"
#include "components/ReadoutFigures.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "widgets/HistogramWidget.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

W_OBJECT_IMPL(an::ui::AnCompactReadout)

namespace an::ui
{

/// The target's astrometry in one line: a globe, "Solved" and the scale and distortion in the monospace; nothing at all
/// when it is not solved.
class CompactAstrometryLine final : public QWidget
{
public:
    explicit CompactAstrometryLine(QWidget* parent)
        : QWidget(parent), m_globe(QStringLiteral(":/AfterNight/icons/SVG/global.svg"))
    {
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        setToolTip(tr("Astrometry of the target image"));
    }

    void setLine(const QString& line)
    {
        m_line = line;
        setAccessibleName(line.isEmpty() ? QString() : tr("Solved, %1").arg(line));
        updateGeometry();
        update();
    }

    [[nodiscard]] const QString& line() const noexcept
    {
        return m_line;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        if (m_line.isEmpty())
        {
            return QSize(0, parts::kCompactReadoutHeader);
        }
        const DesignTokens& t = tokens();
        return QSize(parts::kCompactReadoutIcon + parts::kReadoutDotGap + parts::textWidth(captionFont(t), solved()) +
                         parts::kReadoutDotGap + parts::textWidth(figureFont(t), m_line),
                     parts::kCompactReadoutHeader);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (m_line.isEmpty())
        {
            return;
        }
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal icon = parts::kCompactReadoutIcon;
        parts::paintIcon(painter, m_globe, QRectF(0, (height() - icon) / 2.0, icon, icon), t.colors.textSecondary);
        int x = parts::kCompactReadoutIcon + parts::kReadoutDotGap;
        const QFont caption = captionFont(t);
        const int solvedWidth = parts::textWidth(caption, solved());
        painter.setFont(caption);
        painter.setPen(t.colors.textSecondary);
        painter.drawText(QRect(x, 0, solvedWidth, height()), Qt::AlignLeft | Qt::AlignVCenter, solved());
        x += solvedWidth + parts::kReadoutDotGap;
        const QFont figure = figureFont(t);
        painter.setFont(figure);
        painter.drawText(QRect(x, 0, (std::max)(0, width() - x), height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(figure).elidedText(m_line, Qt::ElideRight, (std::max)(0, width() - x)));
    }

private:
    [[nodiscard]] static QString solved()
    {
        return tr("Solved");
    }

    [[nodiscard]] static QFont captionFont(const DesignTokens& t)
    {
        return parts::sizedFont(t.typography.body, t.metrics.fontCaption);
    }

    [[nodiscard]] static QFont figureFont(const DesignTokens& t)
    {
        return parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
    }

    QIcon m_globe;
    QString m_line;
};

namespace
{

void keepLayout(QLayout* layout)
{
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
}

} // namespace

AnCompactReadout::AnCompactReadout(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAccessibleName(tr("Readout of the preview"));
    auto* column = new QVBoxLayout(this);
    keepLayout(column);
    column->setContentsMargins(parts::kCompactReadoutPaddingSide, parts::kCompactReadoutPaddingTop,
                               parts::kCompactReadoutPaddingSide, parts::kCompactReadoutPaddingBottom);

    auto* header = new QHBoxLayout();
    keepLayout(header);
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(new AnReadoutText(AnReadoutText::Style::Eyebrow, tr("Readout · preview"), this), 0,
                      Qt::AlignVCenter);
    header->addStretch(1);
    m_astrometry = new CompactAstrometryLine(this);
    header->addWidget(m_astrometry, 0, Qt::AlignVCenter);
    m_expand = new AnIconButton(
        QStringLiteral("chevron_down"),
        tr("Expand the full readout over the process (Histogram, Statistics, Astrometry, Image)"), this);
    m_expand->setObjectName(QStringLiteral("compactReadoutExpand"));
    // A toggle: checked while the full readout is open over the column, which a second press closes.
    m_expand->setCheckable(true);
    m_expand->setFixedSize(parts::kCompactReadoutHeader, parts::kCompactReadoutHeader);
    header->addWidget(m_expand, 0, Qt::AlignVCenter);
    column->addLayout(header);

    m_well = new AnReadoutWell(this);
    m_well->setMinimumHeight(parts::kCompactReadoutWell);
    m_well->setFixedHeight(parts::kCompactReadoutWell);
    m_plot = new HistogramWidget(nullptr);
    m_plot->setObjectName(QStringLiteral("compactReadoutHistogram"));
    m_plot->setClippingInfoEnabled(false);
    m_well->setPlot(m_plot);
    m_well->setTag(tr("Preview"));
    if (QWidget* tag = m_well->tagWidget())
    {
        tag->setToolTip(tr("The histogram of the preview: what Apply will produce"));
    }

    m_side = new QWidget(this);
    m_side->setFixedWidth(parts::kCompactReadoutSide);
    auto* side = new QVBoxLayout(m_side);
    keepLayout(side);
    side->setContentsMargins(0, 0, 0, 0);
    m_mode = new AnReadoutText(AnReadoutText::Style::Caption, QString(), m_side);
    side->addWidget(m_mode);
    m_clipping = new AnReadoutGrid(1, m_side);
    m_clipping->setCells({ReadoutCell{.label = tr("Clip low"), .value = QStringLiteral("-")},
                          ReadoutCell{.label = tr("Clip high"), .value = QStringLiteral("-")}});
    side->addWidget(m_clipping);
    side->addStretch(1);

    m_cells = new AnReadoutGrid(static_cast<int>(kCompactReadoutStatistics.size()), this);
    m_cells->setToolTip(tr("The statistics of the preview; the small figure under each is its change against the "
                           "original"));

    m_body = new QGridLayout();
    keepLayout(m_body);
    m_body->setContentsMargins(0, 0, 0, 0);
    column->addLayout(m_body);
    arrange(Arrangement{.wide = false, .columns = static_cast<int>(kCompactReadoutStatistics.size()), .width = 0});
    refillCells();

    QObject::connect(m_expand, &QAbstractButton::clicked, this,
                     [this](const bool shown) { fullReadoutToggled(shown); });
}

AnCompactReadout::~AnCompactReadout() = default;

void AnCompactReadout::setAstrometry(const QString& line)
{
    m_astrometry->setLine(line);
}

QString AnCompactReadout::astrometry() const
{
    return m_astrometry->line();
}

void AnCompactReadout::setCounts(std::shared_ptr<const HistogramDisplayData> counts)
{
    if (counts && counts->isValid())
    {
        const ClippingShare share = clippingShare(*counts);
        m_clipping->setCells({ReadoutCell{.label = tr("Clip low"), .value = formatClipShare(share.low)},
                              ReadoutCell{.label = tr("Clip high"), .value = formatClipShare(share.high)}});
    }
    else
    {
        m_clipping->setCells({ReadoutCell{.label = tr("Clip low"), .value = QStringLiteral("-")},
                              ReadoutCell{.label = tr("Clip high"), .value = QStringLiteral("-")}});
    }
    if (m_plot)
    {
        m_plot->setHistogram(std::move(counts));
    }
}

void AnCompactReadout::setMode(const std::size_t channels, const HistogramResolution resolution)
{
    if (m_plot && m_plot->getHistogramResolution() != resolution)
    {
        m_plot->setHistogramResolution(resolution);
    }
    m_mode->setText(channels == 0u ? QString()
                                   : (channels >= 3u ? tr("RGB") : tr("Grayscale")) + QStringLiteral(" · ") +
                                         histogramResolutionShortName(resolution));
}

void AnCompactReadout::setFigures(std::optional<ImageStatisticsReport> preview,
                                  std::optional<ImageStatisticsReport> original)
{
    m_preview = std::move(preview);
    m_original = std::move(original);
    refillCells();
}

void AnCompactReadout::setStale(const bool stale)
{
    if (m_stale == stale)
    {
        return;
    }
    m_stale = stale;
    m_cells->setStale(stale);
    m_clipping->setStale(stale);
    update();
}

bool AnCompactReadout::isStale() const noexcept
{
    return m_stale;
}

bool AnCompactReadout::isWide() const noexcept
{
    return m_arrangement.wide;
}

QString AnCompactReadout::modeText() const
{
    return m_mode->text();
}

AnReadoutGrid* AnCompactReadout::cells() const noexcept
{
    return m_cells;
}

AnReadoutGrid* AnCompactReadout::clipping() const noexcept
{
    return m_clipping;
}

AnReadoutWell* AnCompactReadout::well() const noexcept
{
    return m_well;
}

HistogramWidget* AnCompactReadout::histogramWidget() const noexcept
{
    return m_plot.data();
}

void AnCompactReadout::setFullReadoutShown(const bool shown)
{
    const QSignalBlocker quiet(m_expand);
    m_expand->setChecked(shown);
}

bool AnCompactReadout::isFullReadoutShown() const noexcept
{
    return m_expand->isChecked();
}

AnIconButton* AnCompactReadout::expandButton() const noexcept
{
    return m_expand;
}

QWidget* AnCompactReadout::astrometryLine() const noexcept
{
    return m_astrometry;
}

void AnCompactReadout::refillCells()
{
    std::vector<ReadoutCell> cells;
    for (const ReadoutStatistic statistic : kCompactReadoutStatistics)
    {
        ReadoutCell cell{.label = readoutStatisticName(statistic), .value = QStringLiteral("-")};
        if (m_preview.has_value())
        {
            const double shown = readoutImageFigure(*m_preview, statistic, StatisticsScale::Normalized);
            cell.value = formatReadoutFigure(shown, StatisticsScale::Normalized);
            if (m_original.has_value())
            {
                const double before = readoutImageFigure(*m_original, statistic, StatisticsScale::Normalized);
                cell.note = QStringLiteral("Δ ") + formatReadoutDelta(shown - before);
            }
        }
        cells.push_back(std::move(cell));
    }
    m_cells->setCells(std::move(cells));
    // Other figures may need other room.
    rearrange();
}

namespace
{

/// How many of `count` cells go to a row when at most `most` fit there: rows as even as they can be - five in one row,
/// three over two, two over two over one.
[[nodiscard]] int evenColumns(const int count, const int most)
{
    const int columns = std::clamp(most, 1, (std::max)(1, count));
    const int rows = (count + columns - 1) / columns;
    return (count + rows - 1) / rows;
}

} // namespace

AnCompactReadout::Arrangement AnCompactReadout::arrangementAt(const int width) const
{
    const int count = (std::max)(1, static_cast<int>(m_cells->cells().size()));
    const int widest = m_cells->widestCell();
    const int gap = parts::kReadoutCellGap;
    const auto across = [widest, gap](const int columns) { return columns * widest + (columns - 1) * gap; };
    const int inner = width - 2 * parts::kCompactReadoutPaddingSide;
    if (width >= parts::kCompactReadoutWide)
    {
        // Beside the histogram, in one row or two, where every cell reads whole and the histogram keeps its least.
        const int beside = inner - parts::kCompactReadoutSide - 2 * (std::max)(0, m_body->horizontalSpacing());
        for (const int columns : {count, evenColumns(count, (count + 1) / 2)})
        {
            if (beside - across(columns) >= parts::kCompactReadoutWideWell)
            {
                return Arrangement{.wide = true, .columns = columns, .width = across(columns)};
            }
        }
    }
    // Under the histogram, as many to a row as read whole there.
    const int fit = (inner + gap) / (std::max)(1, widest + gap);
    return Arrangement{.wide = false, .columns = evenColumns(count, fit), .width = 0};
}

void AnCompactReadout::rearrange()
{
    if (width() <= 0)
    {
        return;
    }
    const Arrangement wanted = arrangementAt(width());
    if (!(wanted == m_arrangement))
    {
        arrange(wanted);
    }
}

void AnCompactReadout::arrange(const Arrangement& arrangement)
{
    m_arrangement = arrangement;
    m_cells->setColumns(arrangement.columns);
    m_body->removeWidget(m_well);
    m_body->removeWidget(m_side);
    m_body->removeWidget(m_cells);
    m_body->addWidget(m_well, 0, 0);
    m_body->addWidget(m_side, 0, 1);
    if (arrangement.wide)
    {
        // The cells take the room they read whole in; the histogram takes the rest.
        m_cells->setFixedWidth(arrangement.width);
        m_body->addWidget(m_cells, 0, 2, Qt::AlignVCenter);
    }
    else
    {
        m_cells->setMinimumWidth(0);
        m_cells->setMaximumWidth(QWIDGETSIZE_MAX);
        m_body->addWidget(m_cells, 1, 0, 1, 2);
    }
    m_body->setColumnStretch(0, 1);
    m_body->setColumnStretch(2, 0);
    // The cells move now, not at the next layout pass: a resize has already laid out the old arrangement.
    if (layout() != nullptr)
    {
        layout()->activate();
    }
    updateGeometry();
}

void AnCompactReadout::applyMetrics()
{
    const DesignTokens& t = tokens();
    layout()->setSpacing(t.metrics.gap);
    m_body->setHorizontalSpacing(t.metrics.pad);
    m_body->setVerticalSpacing(t.metrics.gap);
    // The type and the spacing decide what reads whole where.
    rearrange();
    updateGeometry();
}

bool AnCompactReadout::event(QEvent* event)
{
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)
    {
        applyMetrics();
    }
    return QWidget::event(event);
}

void AnCompactReadout::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (event->size().width() != event->oldSize().width())
    {
        rearrange();
    }
}

void AnCompactReadout::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), tokens().layers.surface2);
}

} // namespace an::ui
