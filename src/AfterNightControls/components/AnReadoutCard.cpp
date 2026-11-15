// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnReadoutCard.h"

#include "components/AnIconButton.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <algorithm>

W_OBJECT_IMPL(an::ui::AnReadoutCardHeader)
W_OBJECT_IMPL(an::ui::AnReadoutCard)

namespace
{

QFont titleFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::capsFont(t.typography.body, t.metrics.fontCaption);
}

QFont summaryFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontSmall);
}

/// A layout that keeps the spacing and margins it was given: the proxy style snaps others to its scale.
void keepLayout(QLayout* layout)
{
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
}

} // namespace

namespace an::ui
{

AnReadoutCardHeader::AnReadoutCardHeader(const QString& title, QWidget* parent) : QAbstractButton(parent)
{
    setText(title);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* row = new QHBoxLayout(this);
    keepLayout(row);
    row->addStretch(1);
    m_tools = new QHBoxLayout();
    keepLayout(m_tools);
    m_tools->setSpacing(parts::kReadoutToolGap);
    row->addLayout(m_tools);
    // The room around the title and the tools is sized from the tokens when the header is polished (applyMetrics).
    describe();
}

void AnReadoutCardHeader::applyMetrics()
{
    layout()->setContentsMargins(tokens().metrics.pad, 0, parts::kReadoutToolGap * 2, 0);
    updateGeometry();
}

bool AnReadoutCardHeader::event(QEvent* event)
{
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)
    {
        applyMetrics();
    }
    return QAbstractButton::event(event);
}

QString AnReadoutCardHeader::title() const
{
    return text();
}

void AnReadoutCardHeader::setSummary(const QString& summary)
{
    if (m_summary == summary)
    {
        return;
    }
    m_summary = summary;
    describe();
    update();
}

QString AnReadoutCardHeader::summary() const
{
    return m_summary;
}

QHBoxLayout* AnReadoutCardHeader::toolRow() const noexcept
{
    return m_tools;
}

void AnReadoutCardHeader::setFolded(const bool folded)
{
    m_folded = folded;
    describe();
}

void AnReadoutCardHeader::describe()
{
    setAccessibleName(m_folded ? tr("%1, folded").arg(text()) : text());
    setAccessibleDescription(m_summary);
}

QRect AnReadoutCardHeader::summaryRect() const
{
    const DesignTokens& t = tokens();
    const int left = t.metrics.pad + parts::textWidth(titleFont(t), text()) + t.metrics.gap;
    const QRect tools = m_tools->geometry();
    const int right = (m_tools->count() > 0 && tools.isValid() ? tools.left() : width()) - t.metrics.gap;
    return QRect(left, 0, (std::max)(0, right - left), height());
}

QSize AnReadoutCardHeader::sizeHint() const
{
    ensurePolished();
    const DesignTokens& t = tokens();
    const int tools = m_tools->sizeHint().width();
    return QSize(t.metrics.pad + parts::textWidth(titleFont(t), text()) + t.metrics.gap + tools +
                     parts::kReadoutToolGap * 2,
                 t.metrics.sectionHeader);
}

QSize AnReadoutCardHeader::minimumSizeHint() const
{
    return QSize(tokens().metrics.pad * 2, tokens().metrics.sectionHeader);
}

void AnReadoutCardHeader::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (hasFocus() && isEnabled())
    {
        parts::paintFocusRing(painter, QRectF(rect()), parts::kReadoutCardRadius);
    }

    const QFont title = titleFont(t);
    painter.setFont(title);
    painter.setPen(t.colors.textSecondary);
    painter.drawText(QRectF(t.metrics.pad, 0, parts::textWidth(title, text()), height()),
                     Qt::AlignLeft | Qt::AlignVCenter, text());

    if (!m_summary.isEmpty())
    {
        const QRect room = summaryRect();
        const QFont font = summaryFont(t);
        painter.setFont(font);
        painter.setPen(t.colors.textSecondary);
        const QString shown = QFontMetricsF(font).elidedText(m_summary, Qt::ElideMiddle, room.width());
        painter.drawText(room, Qt::AlignRight | Qt::AlignVCenter, shown);
    }
}

void AnReadoutCardHeader::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->isAutoRepeat())
    {
        click();
        event->accept();
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

AnReadoutCard::AnReadoutCard(const QString& title, QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* column = new QVBoxLayout(this);
    keepLayout(column);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_header = new AnReadoutCardHeader(title, this);
    column->addWidget(m_header);

    m_body = new QWidget(this);
    m_bodyLayout = new QVBoxLayout(m_body);
    keepLayout(m_bodyLayout);
    // The body's padding and gaps are sized from the tokens when the card is polished (applyMetrics).
    column->addWidget(m_body, 1);

    QObject::connect(m_header, &QAbstractButton::clicked, this, [this]() { setFolded(!m_folded); });
}

QString AnReadoutCard::title() const
{
    return m_header->title();
}

void AnReadoutCard::setSummary(const QString& summary)
{
    m_header->setSummary(summary);
}

QString AnReadoutCard::summary() const
{
    return m_header->summary();
}

AnIconButton* AnReadoutCard::addHeaderTool(const QString& iconKey, const QString& text)
{
    auto* tool = new AnIconButton(iconKey, text, m_header);
    m_header->toolRow()->addWidget(tool);
    return tool;
}

void AnReadoutCard::addHeaderWidget(QWidget* widget)
{
    widget->setParent(m_header);
    m_header->toolRow()->addWidget(widget, 0, Qt::AlignVCenter);
}

AnReadoutCardHeader* AnReadoutCard::header() const noexcept
{
    return m_header;
}

QVBoxLayout* AnReadoutCard::bodyLayout() const noexcept
{
    return m_bodyLayout;
}

QWidget* AnReadoutCard::body() const noexcept
{
    return m_body;
}

bool AnReadoutCard::isFolded() const noexcept
{
    return m_folded;
}

void AnReadoutCard::setFolded(const bool folded)
{
    if (m_folded == folded)
    {
        return;
    }
    m_folded = folded;
    m_body->setVisible(!folded);
    m_header->setFolded(folded);
    updateGeometry();
    foldedChanged(folded);
}

bool AnReadoutCard::isStale() const noexcept
{
    return m_stale;
}

void AnReadoutCard::setStale(const bool stale)
{
    if (m_stale == stale)
    {
        return;
    }
    m_stale = stale;
    staleChanged();
    update();
}

void AnReadoutCard::staleChanged() {}

void AnReadoutCard::applyMetrics()
{
    const DesignTokens& t = tokens();
    m_bodyLayout->setContentsMargins(t.metrics.pad, 0, t.metrics.pad, t.metrics.pad);
    m_bodyLayout->setSpacing(t.metrics.gap);
    updateGeometry();
}

bool AnReadoutCard::event(QEvent* event)
{
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)
    {
        applyMetrics();
    }
    return QWidget::event(event);
}

void AnReadoutCard::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.layers.surface2);
    painter.drawRoundedRect(QRectF(rect()), parts::kReadoutCardRadius, parts::kReadoutCardRadius);
}

} // namespace an::ui
