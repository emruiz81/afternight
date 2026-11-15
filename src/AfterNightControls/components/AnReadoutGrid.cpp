// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnReadoutGrid.h"

#include "components/AnTag.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{

QFont labelFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::capsFont(t.typography.body, t.metrics.fontCaption);
}

QFont valueFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.monospace, t.metrics.fontBody);
}

QFont unitFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontCaption);
}

QFont noteFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
}

int lineHeight(const QFont& font)
{
    return static_cast<int>(std::ceil(QFontMetricsF(font).height()));
}

} // namespace

namespace an::ui
{

AnReadoutGrid::AnReadoutGrid(const int columns, QWidget* parent) : QWidget(parent), m_columns((std::max)(1, columns))
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void AnReadoutGrid::setCells(std::vector<ReadoutCell> cells)
{
    if (m_cells == cells)
    {
        return;
    }
    const bool reshaped = cells.size() != m_cells.size();
    m_cells = std::move(cells);
    describe();
    if (reshaped)
    {
        updateGeometry();
    }
    update();
}

const std::vector<ReadoutCell>& AnReadoutGrid::cells() const noexcept
{
    return m_cells;
}

int AnReadoutGrid::columns() const noexcept
{
    return m_columns;
}

void AnReadoutGrid::setColumns(const int columns)
{
    const int clamped = (std::max)(1, columns);
    if (clamped == m_columns)
    {
        return;
    }
    m_columns = clamped;
    updateGeometry();
    update();
}

void AnReadoutGrid::setStale(const bool stale)
{
    if (m_stale == stale)
    {
        return;
    }
    m_stale = stale;
    update();
}

bool AnReadoutGrid::isStale() const noexcept
{
    return m_stale;
}

int AnReadoutGrid::rows() const
{
    return static_cast<int>((m_cells.size() + static_cast<std::size_t>(m_columns) - 1u) /
                            static_cast<std::size_t>(m_columns));
}

int AnReadoutGrid::cellHeight() const
{
    const DesignTokens& t = tokens();
    int height = lineHeight(labelFont(t)) + parts::kReadoutValueGap + lineHeight(valueFont(t));
    if (std::any_of(m_cells.begin(), m_cells.end(), [](const ReadoutCell& cell) { return !cell.note.isEmpty(); }))
    {
        height += lineHeight(noteFont(t));
    }
    return height;
}

QRect AnReadoutGrid::cellRect(const int index) const
{
    if (index < 0 || index >= static_cast<int>(m_cells.size()))
    {
        return {};
    }
    const int gap = parts::kReadoutCellGap;
    const int cellWidth = (std::max)(0, (width() - gap * (m_columns - 1)) / m_columns);
    const int row = index / m_columns;
    const int column = index % m_columns;
    const int height = cellHeight();
    return QRect(column * (cellWidth + gap), row * (height + gap), cellWidth, height);
}

bool AnReadoutGrid::hasHeightForWidth() const
{
    return true;
}

int AnReadoutGrid::heightForWidth(int /*width*/) const
{
    const int count = rows();
    return count == 0 ? 0 : count * cellHeight() + (count - 1) * parts::kReadoutCellGap;
}

QSize AnReadoutGrid::sizeHint() const
{
    const DesignTokens& t = tokens();
    int widest = 0;
    for (const ReadoutCell& cell : m_cells)
    {
        int value = parts::textWidth(valueFont(t), cell.value);
        if (!cell.unit.isEmpty())
        {
            value += parts::kReadoutDotGap + parts::textWidth(unitFont(t), cell.unit);
        }
        widest = (std::max)({widest, value, parts::textWidth(labelFont(t), cell.label),
                             parts::textWidth(noteFont(t), cell.note)});
    }
    return QSize(m_columns * widest + (m_columns - 1) * parts::kReadoutCellGap, heightForWidth(0));
}

QSize AnReadoutGrid::minimumSizeHint() const
{
    return QSize(0, heightForWidth(0));
}

void AnReadoutGrid::describe()
{
    QStringList parts;
    for (const ReadoutCell& cell : m_cells)
    {
        parts.append(cell.unit.isEmpty() ? QStringLiteral("%1 %2").arg(cell.label, cell.value)
                                         : QStringLiteral("%1 %2 %3").arg(cell.label, cell.value, cell.unit));
    }
    setAccessibleDescription(parts.join(QStringLiteral(", ")));
}

void AnReadoutGrid::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    const QFont label = labelFont(t);
    const QFont value = valueFont(t);
    const QFont unit = unitFont(t);
    const QFont note = noteFont(t);
    // The metrics of each font once a paint, not once a cell.
    const QFontMetricsF labelMetrics(label);
    const QFontMetricsF valueMetrics(value);
    const QFontMetricsF noteMetrics(note);
    const int labelHeight = static_cast<int>(std::ceil(labelMetrics.height()));
    const int valueHeight = static_cast<int>(std::ceil(valueMetrics.height()));
    const int noteHeight = static_cast<int>(std::ceil(noteMetrics.height()));
    const qreal valueOpacity = m_stale ? parts::kReadoutStaleOpacity : 1.0;

    for (int index = 0; index < static_cast<int>(m_cells.size()); ++index)
    {
        const ReadoutCell& cell = m_cells[static_cast<std::size_t>(index)];
        const QRect box = cellRect(index);

        painter.setOpacity(1.0);
        painter.setFont(label);
        painter.setPen(t.colors.textTertiary);
        painter.drawText(QRect(box.left(), box.top(), box.width(), labelHeight), Qt::AlignLeft | Qt::AlignVCenter,
                         labelMetrics.elidedText(cell.label, Qt::ElideRight, box.width()));

        painter.setOpacity(valueOpacity);
        const int valueTop = box.top() + labelHeight + parts::kReadoutValueGap;
        const int unitWidth = cell.unit.isEmpty() ? 0 : parts::kReadoutDotGap + parts::textWidth(unit, cell.unit);
        painter.setFont(value);
        painter.setPen(t.colors.textPrimary);
        const QString shownValue =
            valueMetrics.elidedText(cell.value, Qt::ElideRight, (std::max)(0, box.width() - unitWidth));
        const int valueWidth = static_cast<int>(std::ceil(valueMetrics.horizontalAdvance(shownValue)));
        painter.drawText(QRect(box.left(), valueTop, valueWidth, valueHeight), Qt::AlignLeft | Qt::AlignVCenter,
                         shownValue);
        if (!cell.unit.isEmpty())
        {
            painter.setFont(unit);
            painter.setPen(t.colors.textSecondary);
            painter.drawText(QRect(box.left() + valueWidth + parts::kReadoutDotGap, valueTop,
                                   unitWidth - parts::kReadoutDotGap, valueHeight),
                             Qt::AlignLeft | Qt::AlignVCenter, cell.unit);
        }
        if (!cell.note.isEmpty())
        {
            painter.setFont(note);
            painter.setPen(t.colors.textSecondary);
            painter.drawText(QRect(box.left(), valueTop + valueHeight, box.width(), noteHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             noteMetrics.elidedText(cell.note, Qt::ElideRight, box.width()));
        }
    }
}

AnReadoutText::AnReadoutText(const Style style, const QString& text, QWidget* parent)
    : QWidget(parent), m_style(style), m_text(text)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAccessibleName(text);
}

void AnReadoutText::setText(const QString& text)
{
    if (m_text == text)
    {
        return;
    }
    m_text = text;
    setAccessibleName(text);
    updateGeometry();
    update();
}

QString AnReadoutText::text() const
{
    return m_text;
}

QFont AnReadoutText::shownFont() const
{
    const DesignTokens& t = tokens();
    switch (m_style)
    {
    case Style::Eyebrow:
        return parts::capsFont(t.typography.body, t.metrics.fontCaption);
    case Style::Caption:
        return parts::sizedFont(t.typography.body, t.metrics.fontCaption);
    case Style::Figure:
        return parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
    }
    return parts::sizedFont(t.typography.body, t.metrics.fontCaption);
}

QSize AnReadoutText::sizeHint() const
{
    const QFont shown = shownFont();
    return QSize(parts::textWidth(shown, m_text), lineHeight(shown));
}

QSize AnReadoutText::minimumSizeHint() const
{
    return QSize(0, lineHeight(shownFont()));
}

void AnReadoutText::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    const QFont shown = shownFont();
    painter.setFont(shown);
    painter.setPen(m_style == Style::Eyebrow ? t.colors.textTertiary : t.colors.textSecondary);
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetricsF(shown).elidedText(m_text, Qt::ElideRight, width()));
}

AnReadoutWell::AnReadoutWell(QWidget* parent) : QWidget(parent)
{
    auto* inside = new QVBoxLayout(this);
    inside->setProperty("anKeepLayoutMargins", true);
    inside->setProperty("anKeepLayoutSpacing", true);
    inside->setContentsMargins(parts::kReadoutWellInset, parts::kReadoutWellInset, parts::kReadoutWellInset,
                               parts::kReadoutWellInset);
    inside->setSpacing(0);
    auto* holder = new QWidget(this);
    m_stack = new QStackedLayout(holder);
    m_stack->setStackingMode(QStackedLayout::StackAll);
    inside->addWidget(holder);
    setMinimumHeight(parts::kReadoutWellMinimum);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
}

void AnReadoutWell::setPlot(QWidget* plot)
{
    m_stack->insertWidget(0, plot);
    if (m_overlay != nullptr)
    {
        m_overlay->raise();
    }
}

void AnReadoutWell::setTag(const QString& tag)
{
    if (tag.isEmpty())
    {
        delete m_overlay;
        m_overlay = nullptr;
        m_tag = nullptr;
        return;
    }
    if (m_overlay == nullptr)
    {
        // The tag sits over the plot in a layer that lets the pointer through to it.
        m_overlay = new QWidget();
        m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* row = new QHBoxLayout(m_overlay);
        row->setProperty("anKeepLayoutMargins", true);
        row->setContentsMargins(parts::kReadoutWellInset, parts::kReadoutWellInset, 0, 0);
        m_tag = new AnTag(tag, AnTag::Tone::Neutral, m_overlay);
        row->addWidget(m_tag, 0, Qt::AlignLeft | Qt::AlignTop);
        row->addStretch(1);
        m_stack->addWidget(m_overlay);
        m_overlay->raise();
        m_overlay->show();
        return;
    }
    static_cast<AnTag*>(m_tag)->setText(tag);
}

QString AnReadoutWell::tag() const
{
    return m_tag != nullptr ? static_cast<AnTag*>(m_tag)->text() : QString();
}

QWidget* AnReadoutWell::tagWidget() const noexcept
{
    return m_tag;
}

void AnReadoutWell::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tokens().colors.bgWell);
    painter.drawRoundedRect(QRectF(rect()), parts::kReadoutWellRadius, parts::kReadoutWellRadius);
}

} // namespace an::ui
