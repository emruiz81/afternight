// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the window layout cases share: whether a layout holds a widget, the widgets a window places itself rather than
/// through its layout, and the widgets whose font was chosen outside the design tokens' typography.
#pragma once

#include "theme/AnDesignTokens.h"

#include <QApplication>
#include <QFont>
#include <QLayout>
#include <QLayoutItem>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <array>

namespace an::tests::processes::framework
{

/// A widget's object name, or its class when it has none.
[[nodiscard]] inline QString widgetName(const QWidget& widget)
{
    return widget.objectName().isEmpty() ? QString::fromLatin1(widget.metaObject()->className()) : widget.objectName();
}

/// Whether a layout, or a layout nested in it, holds a widget.
[[nodiscard]] inline bool layoutHolds(const QLayout& layout, const QWidget& widget)
{
    for (int index = 0; index < layout.count(); ++index)
    {
        QLayoutItem* item = layout.itemAt(index);
        if (item == nullptr)
        {
            continue;
        }
        if (item->widget() == &widget)
        {
            return true;
        }
        if (const QLayout* nested = item->layout(); nested != nullptr && layoutHolds(*nested, widget))
        {
            return true;
        }
    }
    return false;
}

/// The widgets a window places itself rather than through its layout: each child widget of the window that is not a
/// window of its own and was not hidden on purpose, and that the window's layout does not hold. Such a widget keeps
/// the geometry it was given, so it neither grows nor shrinks with the window.
[[nodiscard]] inline QStringList widgetsOutsideTheLayout(const QWidget& window)
{
    QStringList outside;
    const QLayout* layout = window.layout();
    for (const QWidget* child : window.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
    {
        if (child->isWindow() || (child->testAttribute(Qt::WA_WState_ExplicitShowHide) && child->isHidden()))
        {
            continue;
        }
        if (layout == nullptr || !layoutHolds(*layout, *child))
        {
            outside << widgetName(*child);
        }
    }
    return outside;
}

/// The widgets in a window whose font was chosen outside the design tokens' typography: a widget that sets its font
/// must take the point size and family of one of the tokens' faces, or keep the ones it would inherit, so a size set
/// by hand - in points or in pixels - is found.
[[nodiscard]] inline QStringList fontsOutsideTheTokens(const QWidget& window)
{
    const an::ui::TypographyTokens& typography = an::ui::tokens().typography;
    const std::array<const QFont*, 6> faces{&typography.heading1, &typography.heading2, &typography.heading3,
                                            &typography.body,     &typography.caption,  &typography.monospace};
    QStringList outside;
    for (const QWidget* widget : window.findChildren<QWidget*>())
    {
        if (!widget->testAttribute(Qt::WA_SetFont))
        {
            continue;
        }
        const QFont font = widget->font();
        const QFont inherited =
            widget->parentWidget() != nullptr ? widget->parentWidget()->font() : QApplication::font();
        bool sizeOfAFace = font.pointSize() == inherited.pointSize();
        bool familyOfAFace = font.family() == inherited.family();
        for (const QFont* face : faces)
        {
            sizeOfAFace = sizeOfAFace || font.pointSize() == face->pointSize();
            familyOfAFace = familyOfAFace || font.family() == face->family();
        }
        if (font.pointSize() <= 0 || !sizeOfAFace || !familyOfAFace)
        {
            outside << QStringLiteral("%1 (%2, %3 pt)").arg(widgetName(*widget), font.family()).arg(font.pointSize());
        }
    }
    return outside;
}

} // namespace an::tests::processes::framework
