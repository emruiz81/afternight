// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnLabeledField.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QSpacerItem>

#include <algorithm>
#include <cmath>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnLabeledField)

namespace
{

QFont labelFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontBody);
}

QFont hintFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontCaption);
}

int lineHeight(const QFont& font)
{
    return static_cast<int>(std::ceil(QFontMetricsF(font).height()));
}

} // namespace

namespace an::ui
{

AnLabeledField::AnLabeledField(const QString& label, QWidget* field, const LabelPlacement placement, QWidget* parent)
    : QWidget(parent), m_label(label), m_field(field), m_placement(placement),
      m_labelArea(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed))
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // The layout's room and gaps are sized from the tokens when the widget is polished (applyMetrics).
    if (m_placement == LabelPlacement::Beside)
    {
        // A layout adds no spacing beside a spacer item, so the label's room holds the gap before the field.
        auto* const grid = new QGridLayout(this);
        grid->setProperty("anKeepLayoutMargins", true);
        grid->setProperty("anKeepLayoutSpacing", true);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(0);
        grid->addItem(m_labelArea, 0, 0);
        if (m_field != nullptr)
        {
            grid->addWidget(m_field, 0, 1, Qt::AlignVCenter);
        }
        grid->setColumnStretch(1, 1);
    }
    else
    {
        // A layout adds no spacing beside a spacer item, so the label's and the hint's rooms hold their gaps.
        auto* const column = new QVBoxLayout(this);
        column->setProperty("anKeepLayoutMargins", true);
        column->setProperty("anKeepLayoutSpacing", true);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        column->addItem(m_labelArea);
        if (m_field != nullptr)
        {
            column->addWidget(m_field);
        }
        m_hintArea = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        column->addItem(m_hintArea);
    }
    nameTheField();
}

void AnLabeledField::setLabelText(const QString& label)
{
    if (m_label == label)
    {
        return;
    }
    m_label = label;
    nameTheField();
    applyMetrics();
    update();
}

QString AnLabeledField::labelText() const
{
    return m_label;
}

void AnLabeledField::setHint(const QString& hint)
{
    if (m_hint == hint)
    {
        return;
    }
    m_hint = hint;
    nameTheField();
    applyMetrics();
    update();
}

QString AnLabeledField::hint() const
{
    return m_hint;
}

void AnLabeledField::setLabelWidth(const int width)
{
    const int column = (std::max)(0, width);
    if (m_labelWidth == column)
    {
        return;
    }
    m_labelWidth = column;
    applyMetrics();
    update();
}

int AnLabeledField::labelWidth() const noexcept
{
    return m_labelWidth;
}

QWidget* AnLabeledField::field() const noexcept
{
    return m_field;
}

AnLabeledField::LabelPlacement AnLabeledField::placement() const noexcept
{
    return m_placement;
}

QSize AnLabeledField::sizeHint() const
{
    ensurePolished();
    return QWidget::sizeHint();
}

QSize AnLabeledField::minimumSizeHint() const
{
    ensurePolished();
    return QWidget::minimumSizeHint();
}

void AnLabeledField::applyMetrics()
{
    const DesignTokens& t = tokens();
    const QFont label = labelFont(t);
    const QFont hint = hintFont(t);
    const int labelLine = lineHeight(label);
    const int hintLine = m_hint.isEmpty() ? 0 : lineHeight(hint);
    const int labelText = parts::textWidth(label, m_label);
    const int hintText = m_hint.isEmpty() ? 0 : parts::textWidth(hint, m_hint);
    if (m_placement == LabelPlacement::Beside)
    {
        // The label's column - as asked, or as wide as the label and the hint under it - and the gap before the field,
        // as high as the label and the hint.
        const int column = m_labelWidth > 0 ? m_labelWidth : (std::max)(labelText, hintText);
        m_labelArea->changeSize(column + t.metrics.gap,
                                labelLine + (hintLine > 0 ? parts::kFieldLabelGapAbove + hintLine : 0),
                                QSizePolicy::Fixed, QSizePolicy::Fixed);
        static_cast<QGridLayout*>(layout())->setRowMinimumHeight(0, t.metrics.control);
        setToolTip(labelText > column ? m_label : QString());
    }
    else
    {
        m_labelArea->changeSize(labelText, labelLine + parts::kFieldLabelGapAbove, QSizePolicy::Fixed,
                                QSizePolicy::Fixed);
        m_hintArea->changeSize(hintText, hintLine > 0 ? parts::kFieldLabelGapAbove + hintLine : 0, QSizePolicy::Fixed,
                               QSizePolicy::Fixed);
        setToolTip(QString());
    }
    layout()->invalidate();
    updateGeometry();
}

QRect AnLabeledField::labelRect() const
{
    const DesignTokens& t = tokens();
    const QRect area = m_labelArea->geometry();
    const int labelLine = lineHeight(labelFont(t));
    if (m_placement == LabelPlacement::Beside)
    {
        // The label and the hint under it, as one block in the middle of the row, clear of the gap after it.
        const int block = m_labelArea->sizeHint().height();
        return QRect(area.left(), area.top() + (area.height() - block) / 2, area.width() - t.metrics.gap, labelLine);
    }
    return QRect(area.left(), area.top(), width(), labelLine);
}

QRect AnLabeledField::hintRect() const
{
    if (m_hint.isEmpty())
    {
        return QRect();
    }
    const DesignTokens& t = tokens();
    const int hintLine = lineHeight(hintFont(t));
    if (m_placement == LabelPlacement::Beside)
    {
        const QRect label = labelRect();
        return QRect(label.left(), label.bottom() + 1 + parts::kFieldLabelGapAbove, label.width(), hintLine);
    }
    return QRect(0, m_hintArea->geometry().top() + parts::kFieldLabelGapAbove, width(), hintLine);
}

void AnLabeledField::nameTheField()
{
    if (m_field == nullptr)
    {
        return;
    }
    // The label names the field and the hint describes it, unless the caller named or described it otherwise.
    if (m_field->accessibleName().isEmpty() || m_field->accessibleName() == m_namedAs)
    {
        m_field->setAccessibleName(m_label);
        m_namedAs = m_label;
    }
    if (m_field->accessibleDescription().isEmpty() || m_field->accessibleDescription() == m_describedAs)
    {
        m_field->setAccessibleDescription(m_hint);
        m_describedAs = m_hint;
    }
}

bool AnLabeledField::event(QEvent* event)
{
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)
    {
        applyMetrics();
    }
    return QWidget::event(event);
}

void AnLabeledField::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    if (!isEnabled())
    {
        painter.setOpacity(parts::kDisabledOpacity);
    }
    const QFont label = labelFont(t);
    const QRect labelArea = labelRect();
    painter.setFont(label);
    painter.setPen(t.colors.textPrimary);
    painter.drawText(labelArea, Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetricsF(label).elidedText(m_label, Qt::ElideRight, labelArea.width()));
    if (!m_hint.isEmpty())
    {
        const QFont hint = hintFont(t);
        const QRect hintArea = hintRect();
        painter.setFont(hint);
        painter.setPen(t.colors.textSecondary);
        painter.drawText(hintArea, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(hint).elidedText(m_hint, Qt::ElideRight, hintArea.width()));
    }
}

void AnLabeledField::mousePressEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    if (m_field != nullptr && (labelRect().contains(position) || hintRect().contains(position)))
    {
        m_field->setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

} // namespace an::ui
