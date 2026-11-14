// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnKeyHint.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QKeyCombination>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QSizePolicy>
#include <QStringList>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnKeyHint)

namespace
{

QFont hintFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
}

/// A modifier's name as the platform writes it in a shortcut, without the separator that follows it.
QString modifierName(const Qt::KeyboardModifier modifier)
{
    QString name = QKeySequence(QKeyCombination(modifier, Qt::Key_A)).toString(QKeySequence::NativeText);
    name.chop(QKeySequence(Qt::Key_A).toString(QKeySequence::NativeText).size());
    while (name.endsWith(QLatin1Char('+')))
    {
        name.chop(1);
    }
    return name;
}

} // namespace

namespace an::ui
{

AnKeyHint::AnKeyHint(const QKeySequence& keys, QWidget* parent) : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeySequence(keys);
}

void AnKeyHint::setKeySequence(const QKeySequence& keys)
{
    m_keys = keys;
    setText(keysText(keys));
    setAccessibleName(keys.toString(QKeySequence::NativeText));
}

QKeySequence AnKeyHint::keySequence() const
{
    return m_keys;
}

void AnKeyHint::setSurface(const Surface surface)
{
    if (m_surface == surface)
    {
        return;
    }
    m_surface = surface;
    update();
}

AnKeyHint::Surface AnKeyHint::surface() const noexcept
{
    return m_surface;
}

QString AnKeyHint::keysText(const QKeySequence& keys)
{
    QStringList combinations;
    for (int index = 0; index < keys.count(); ++index)
    {
        const QKeyCombination combination = keys[index];
        QStringList parts;
        for (const Qt::KeyboardModifier modifier :
             {Qt::ControlModifier, Qt::AltModifier, Qt::ShiftModifier, Qt::MetaModifier})
        {
            if (combination.keyboardModifiers().testFlag(modifier))
            {
                parts.append(modifierName(modifier));
            }
        }
        parts.append(QKeySequence(combination.key()).toString(QKeySequence::NativeText));
        combinations.append(parts.join(QLatin1Char(' ')));
    }
    return combinations.join(QStringLiteral(", "));
}

QSize AnKeyHint::sizeHint() const
{
    const DesignTokens& t = tokens();
    // The text, its padding at each side and the hairline at each side.
    return QSize(2 * parts::kKeyHintPadding + 2 + parts::textWidth(hintFont(t), text()), t.metrics.tag - 2);
}

QSize AnKeyHint::minimumSizeHint() const
{
    return sizeHint();
}

void AnKeyHint::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = parts::kKeyHintRadius - 0.5;
    QColor text = t.colors.textSecondary;
    if (m_surface == Surface::Raised)
    {
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.setBrush(t.layers.surface2);
        // The fill under the hairline, so a translucent line is laid over the box as it is everywhere else.
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(rect()), parts::kKeyHintRadius, parts::kKeyHintRadius);
        painter.restore();
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, radius, radius);
    }
    else
    {
        text = palette().color(QPalette::WindowText);
        painter.setPen(QPen(parts::atShare(text, parts::kKeyHintOnColourOutline), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, radius, radius);
    }
    painter.setPen(text);
    painter.setFont(hintFont(t));
    painter.drawText(QRectF(rect()), Qt::AlignCenter, this->text());
}

} // namespace an::ui
