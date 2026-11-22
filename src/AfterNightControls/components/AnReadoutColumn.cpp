// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnReadoutColumn.h"

#include "components/AnAstrometryCard.h"
#include "components/AnElidedLabel.h"
#include "components/AnHistogramCard.h"
#include "components/AnIconButton.h"
#include "components/AnImageFactsCard.h"
#include "components/AnLayoutUtils.h"
#include "components/AnReadoutGrid.h"
#include "components/AnStatisticsCard.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <array>

W_OBJECT_IMPL(an::ui::AnReadoutColumn)

namespace an::ui
{
namespace
{

constexpr std::array<ReadoutCardId, 4> kCards{ReadoutCardId::Histogram, ReadoutCardId::Statistics,
                                              ReadoutCardId::Astrometry, ReadoutCardId::Image};

void keepLayout(QLayout* layout)
{
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
}

} // namespace

AnReadoutColumn::AnReadoutColumn(QWidget* parent) : QWidget(parent), m_width(parts::kReadoutColumnWidth)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAccessibleName(tr("Readout"));
    auto* column = new QVBoxLayout(this);
    keepLayout(column);
    column->setSpacing(parts::kReadoutCardGap);

    auto* header = new QHBoxLayout();
    keepLayout(header);
    header->setContentsMargins(0, 0, 0, 0);
    m_eyebrow = new AnReadoutText(AnReadoutText::Style::Eyebrow, tr("Readout"), this);
    header->addWidget(m_eyebrow, 0, Qt::AlignVCenter);
    m_name = new AnElidedLabel(1, this);
    m_name->setObjectName(QStringLiteral("readoutViewName"));
    m_name->setElideMode(Qt::ElideMiddle);
    header->addWidget(m_name, 1, Qt::AlignVCenter);
    m_collapse = new AnIconButton(QStringLiteral("panel_right"), tr("Collapse the readout column"), this);
    header->addWidget(m_collapse, 0, Qt::AlignVCenter);
    column->addLayout(header);

    // The cards scroll under the header where those open need more than the column's height - a person may open every
    // card in a short window, where the Image and Astrometry cards start folded. Where they fit, nothing scrolls and
    // the cards lie as they would without the area.
    m_cardsArea = new QScrollArea(this);
    m_cardsArea->setObjectName(QStringLiteral("readoutCardsArea"));
    m_cardsArea->setFrameShape(QFrame::NoFrame);
    m_cardsArea->setFocusPolicy(Qt::NoFocus);
    m_cardsArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_cardsArea->setWidgetResizable(true);
    m_cardsArea->setAutoFillBackground(false);
    m_cardsArea->viewport()->setAutoFillBackground(false);
    m_cards = new QWidget(m_cardsArea);
    // As wide as the area, as the column made the cards before they scrolled: each card's rows give way to its width.
    m_cards->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* cards = new QVBoxLayout(m_cards);
    keepLayout(cards);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(parts::kReadoutCardGap);
    m_histogram = new AnHistogramCard(m_cards);
    m_statistics = new AnStatisticsCard(m_cards);
    m_astrometry = new AnAstrometryCard(m_cards);
    m_image = new AnImageFactsCard(m_cards);
    // The histogram grows with the column up to its maximum before any room is left; the room under the last card is
    // left empty, so a tall window does not stretch the plot. Folded it is its header alone, which an explicit maximum
    // would override, so the maximum holds only while it is open.
    const auto capHistogram = [this](const bool folded) {
        m_histogram->setMaximumHeight(folded ? QWIDGETSIZE_MAX : parts::kReadoutHistogramCardMaximum);
    };
    capHistogram(m_histogram->isFolded());
    QObject::connect(m_histogram, &AnReadoutCard::foldedChanged, this, capHistogram);
    cards->addWidget(m_histogram, 1);
    cards->addWidget(m_statistics);
    cards->addWidget(m_astrometry);
    cards->addWidget(m_image);
    cards->addStretch(0);
    m_cardsArea->setWidget(m_cards);
    // Set by the area as it takes the cards; the column's surface is what lies between them.
    m_cards->setAutoFillBackground(false);
    keepScrollContentSizedForItsWidth(m_cardsArea);
    column->addWidget(m_cardsArea, 1);

    for (const ReadoutCardId id : kCards)
    {
        QObject::connect(card(id), &AnReadoutCard::foldedChanged, this, [this](bool) {
            if (!m_restoring)
            {
                m_foldsRemembered = true;
                foldsChanged();
            }
        });
    }
    QObject::connect(m_collapse, &QAbstractButton::clicked, this, [this]() { setCollapsed(!m_collapsed); });
    setFixedWidth(m_width);
}

AnReadoutColumn::~AnReadoutColumn() = default;

void AnReadoutColumn::setViewName(const QString& name)
{
    m_name->setFullText(name);
}

QString AnReadoutColumn::viewName() const
{
    return m_name->fullText();
}

AnHistogramCard* AnReadoutColumn::histogramCard() const noexcept
{
    return m_histogram;
}

AnStatisticsCard* AnReadoutColumn::statisticsCard() const noexcept
{
    return m_statistics;
}

AnAstrometryCard* AnReadoutColumn::astrometryCard() const noexcept
{
    return m_astrometry;
}

AnImageFactsCard* AnReadoutColumn::imageCard() const noexcept
{
    return m_image;
}

AnReadoutCard* AnReadoutColumn::card(const ReadoutCardId id) const
{
    switch (id)
    {
    case ReadoutCardId::Histogram:
        return m_histogram;
    case ReadoutCardId::Statistics:
        return m_statistics;
    case ReadoutCardId::Astrometry:
        return m_astrometry;
    case ReadoutCardId::Image:
        return m_image;
    }
    return nullptr;
}

QString AnReadoutColumn::cardName(const ReadoutCardId id)
{
    switch (id)
    {
    case ReadoutCardId::Histogram:
        return QStringLiteral("histogram");
    case ReadoutCardId::Statistics:
        return QStringLiteral("statistics");
    case ReadoutCardId::Astrometry:
        return QStringLiteral("astrometry");
    case ReadoutCardId::Image:
        return QStringLiteral("image");
    }
    return QString();
}

QString AnReadoutColumn::foldedCards() const
{
    QStringList names;
    for (const ReadoutCardId id : kCards)
    {
        if (card(id)->isFolded())
        {
            names.append(cardName(id));
        }
    }
    return names.join(QLatin1Char(','));
}

void AnReadoutColumn::setFoldedCards(const QString& names)
{
    const QStringList folded = names.split(QLatin1Char(','), Qt::SkipEmptyParts);
    m_restoring = true;
    for (const ReadoutCardId id : kCards)
    {
        card(id)->setFolded(folded.contains(cardName(id)));
    }
    m_restoring = false;
    m_foldsRemembered = true;
}

bool AnReadoutColumn::isCollapsed() const noexcept
{
    return m_collapsed;
}

void AnReadoutColumn::setCollapsed(const bool collapsed)
{
    if (m_collapsed == collapsed)
    {
        return;
    }
    m_collapsed = collapsed;
    applyCollapsed();
    collapsedChanged(collapsed);
}

void AnReadoutColumn::setWindowSize(const QSize& size)
{
    m_width =
        size.width() < parts::kReadoutNarrowWindow ? parts::kReadoutColumnNarrowWidth : parts::kReadoutColumnWidth;
    if (!m_foldsRemembered)
    {
        m_restoring = true;
        m_image->setFolded(size.height() < parts::kReadoutShortWindow);
        m_astrometry->setFolded(size.height() < parts::kReadoutShorterWindow);
        m_restoring = false;
    }
    applyCollapsed();
}

int AnReadoutColumn::columnWidth() const noexcept
{
    return m_width;
}

AnElidedLabel* AnReadoutColumn::nameLabel() const noexcept
{
    return m_name;
}

AnIconButton* AnReadoutColumn::collapseButton() const noexcept
{
    return m_collapse;
}

QSize AnReadoutColumn::sizeHint() const
{
    return QSize(width(), QWidget::sizeHint().height());
}

void AnReadoutColumn::applyCollapsed()
{
    m_eyebrow->setVisible(!m_collapsed);
    m_name->setVisible(!m_collapsed);
    m_cardsArea->setVisible(!m_collapsed);
    m_collapse->setDescription(m_collapsed ? tr("Open the readout column") : tr("Collapse the readout column"));
    if (m_collapsed)
    {
        const DesignTokens& t = tokens();
        setFixedWidth(t.metrics.controlXSmall + 2 * t.metrics.pad + 1);
    }
    else
    {
        setFixedWidth(m_width);
    }
}

void AnReadoutColumn::applyMetrics()
{
    const DesignTokens& t = tokens();
    // The hairline at the left is the column's own first pixel.
    layout()->setContentsMargins(t.metrics.pad + 1, t.metrics.gap, t.metrics.pad, t.metrics.pad);
    applyCollapsed();
}

bool AnReadoutColumn::event(QEvent* event)
{
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)
    {
        applyMetrics();
    }
    return QWidget::event(event);
}

void AnReadoutColumn::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.fillRect(rect(), t.layers.surface1);
    painter.fillRect(QRect(0, 0, 1, height()), t.colors.borderSubtle);
}

} // namespace an::ui
