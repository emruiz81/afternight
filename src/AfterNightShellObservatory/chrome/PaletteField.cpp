// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/PaletteField.h"

#include "components/AnKeyHint.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>

namespace an::ui::observatory
{

namespace
{

[[nodiscard]] QString placeholder()
{
    return QCoreApplication::translate("an::ui::observatory::PaletteField", "Search or run a command");
}

} // namespace

PaletteField::PaletteField(QWidget* parent) : QAbstractButton(parent)
{
    setObjectName(QStringLiteral("observatoryPaletteField"));
    setCursor(Qt::IBeamCursor);
    setFocusPolicy(Qt::TabFocus);
    setToolTip(placeholder() + QStringLiteral(" (Ctrl+K)"));
    setAccessibleName(placeholder());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

PaletteField::Form PaletteField::formFor(const int windowWidth)
{
    if (windowWidth < an::ui::parts::kPaletteFieldIconWindow)
    {
        return Form::Icon;
    }
    if (windowWidth < an::ui::parts::kPaletteFieldNarrowWindow)
    {
        return Form::Narrow;
    }
    return Form::Wide;
}

void PaletteField::setIconProvider(std::function<QIcon(const QString& name)> provider)
{
    m_icons = std::move(provider);
    update();
}

void PaletteField::setWindowWidth(const int width)
{
    const Form form = formFor(width);
    if (form == m_form)
    {
        return;
    }
    m_form = form;
    updateGeometry();
    update();
}

PaletteField::Form PaletteField::form() const noexcept
{
    return m_form;
}

QSize PaletteField::sizeHint() const
{
    const int height = an::ui::tokens().metrics.barTop - an::ui::parts::kPaletteFieldInset;
    switch (m_form)
    {
    case Form::Wide:
        return {an::ui::parts::kPaletteFieldWide, height};
    case Form::Narrow:
        return {an::ui::parts::kPaletteFieldNarrow, height};
    case Form::Icon:
        break;
    }
    return {an::ui::parts::kTopBarButton, an::ui::parts::kTopBarButton};
}

QSize PaletteField::minimumSizeHint() const
{
    return sizeHint();
}

void PaletteField::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(body, an::ui::parts::kPaletteFieldRadius, an::ui::parts::kPaletteFieldRadius);
    // An icon button shows its surface only under the pointer, as the bar's other buttons do.
    if (m_form != Form::Icon || underMouse())
    {
        painter.fillPath(shape, t.layers.surface3);
    }
    if (m_form != Form::Icon)
    {
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.drawPath(shape);
    }

    const int iconSide = an::ui::parts::kPaletteFieldIcon;
    const int iconLeft = m_form == Form::Icon ? (width() - iconSide) / 2 : an::ui::parts::kTopBarGap + 2;
    const QRect iconRect(iconLeft, (height() - iconSide) / 2, iconSide, iconSide);
    const QIcon search =
        m_icons ? m_icons(QStringLiteral("search")) : QIcon(QStringLiteral(":/AfterNight/icons/SVG/search.svg"));
    search.paint(&painter, iconRect);
    if (m_form == Form::Icon)
    {
        return;
    }

    // The placeholder, then the key hint at the right.
    QFont font = t.typography.body;
    font.setPixelSize(t.metrics.fontBody);
    painter.setFont(font);
    painter.setPen(t.colors.textTertiary);
    const QString keys = an::ui::AnKeyHint::keysText(QKeySequence(Qt::CTRL | Qt::Key_K));
    const QSize hint = an::ui::AnKeyHint::hintSize(keys);
    const int textLeft = iconRect.right() + an::ui::parts::kTopBarGap;
    const int textRight = width() - an::ui::parts::kTopBarGap - hint.width() - an::ui::parts::kTopBarGap;
    const QRect textRect(textLeft, 0, (std::max)(0, textRight - textLeft), height());
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(font).elidedText(placeholder(), Qt::ElideRight, textRect.width()));

    const QRectF hintRect(width() - an::ui::parts::kTopBarGap - hint.width(), (height() - hint.height()) / 2.0,
                          hint.width(), hint.height());
    an::ui::AnKeyHint::paintHint(painter, hintRect, keys, an::ui::AnKeyHint::Surface::Raised, QColor());
}

void PaletteField::changeEvent(QEvent* event)
{
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        updateGeometry();
        update();
    }
    QAbstractButton::changeEvent(event);
}

} // namespace an::ui::observatory
