// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Whether a window's controls have the room they need: a control shown that lies outside its parent, a button, a field
/// or a box smaller than it needs, a combo box too narrow for its longest item, a label too small for its text, and a
/// scroll area's content shorter than its width needs. Whether text fits depends on how wide the font draws it, and the
/// suites' own platform draws a box font about twice the interface's width: a case asserts what is clipped only where
/// `textIsMeasured()`, and runs natively (`QT_QPA_PLATFORM=windows`) to measure it.

#pragma once

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace an::tests::look
{

/// A widget as a failure names it.
[[nodiscard]] inline QString nameOf(const QWidget* widget)
{
    QString name = QString::fromLatin1(widget->metaObject()->className());
    if (!widget->objectName().isEmpty())
    {
        name += QStringLiteral(" '%1'").arg(widget->objectName());
    }
    if (const auto* label = qobject_cast<const QLabel*>(widget); label != nullptr && !label->text().isEmpty())
    {
        name += QStringLiteral(" \"%1\"").arg(label->text().left(40));
    }
    if (const auto* button = qobject_cast<const QAbstractButton*>(widget);
        button != nullptr && !button->text().isEmpty())
    {
        name += QStringLiteral(" \"%1\"").arg(button->text());
    }
    return name;
}

/// The scroll area whose viewport `widget` is; null for any other widget.
[[nodiscard]] inline const QAbstractScrollArea* scrollAreaOfViewport(const QWidget* widget)
{
    const auto* const area =
        widget != nullptr ? qobject_cast<const QAbstractScrollArea*>(widget->parentWidget()) : nullptr;
    return area != nullptr && area->viewport() == widget ? area : nullptr;
}

/// Every control of a window that is clipped, each named with what is wrong: a widget shown that lies outside its
/// parent - but the content of a scroll area, which may be taller than its viewport and scrolls, and may be wider only
/// where its area scrolls sideways - a button, a field, a box or a spin box smaller than it needs, a combo box too
/// narrow for its longest item, and a label too small for its text: narrower than its one line, or, wrapped, shorter
/// than its lines at its width.
[[nodiscard]] inline QStringList clippedControls(const QWidget& root)
{
    QStringList clipped;
    for (const QWidget* widget : root.findChildren<QWidget*>())
    {
        if (widget->isWindow() || !widget->isVisibleTo(&root))
        {
            continue;
        }
        const QWidget* const parent = widget->parentWidget();
        if (parent == nullptr)
        {
            continue;
        }
        if (const QAbstractScrollArea* const area = scrollAreaOfViewport(parent); area != nullptr)
        {
            const bool scrollsSideways =
                area->horizontalScrollBar() != nullptr && area->horizontalScrollBar()->isVisibleTo(&root);
            if (!scrollsSideways && widget->geometry().right() > parent->rect().right())
            {
                clipped.append(nameOf(widget) + QStringLiteral(" is wider than the area it scrolls in (%1 > %2)")
                                                    .arg(widget->geometry().right() + 1)
                                                    .arg(parent->width()));
            }
        }
        else if (!parent->rect().contains(widget->geometry()))
        {
            clipped.append(nameOf(widget) + QStringLiteral(" lies outside ") + nameOf(parent));
        }

        const QSize size = widget->size();
        if (qobject_cast<const QAbstractButton*>(widget) != nullptr ||
            qobject_cast<const QLineEdit*>(widget) != nullptr ||
            qobject_cast<const QAbstractSpinBox*>(widget) != nullptr)
        {
            const QSize least = widget->minimumSizeHint();
            if (size.width() < least.width() || size.height() < least.height())
            {
                clipped.append(nameOf(widget) + QStringLiteral(" is smaller than it needs (%1 x %2 < %3 x %4)")
                                                    .arg(size.width())
                                                    .arg(size.height())
                                                    .arg(least.width())
                                                    .arg(least.height()));
            }
        }
        else if (const auto* combo = qobject_cast<const QComboBox*>(widget); combo != nullptr)
        {
            const QSize needs = combo->sizeHint();
            if (size.width() < needs.width() || size.height() < combo->minimumSizeHint().height())
            {
                clipped.append(nameOf(widget) + QStringLiteral(" is too narrow for its longest item (%1 < %2)")
                                                    .arg(size.width())
                                                    .arg(needs.width()));
            }
        }
        else if (const auto* label = qobject_cast<const QLabel*>(widget);
                 label != nullptr && !label->text().isEmpty() && label->pixmap().isNull())
        {
            if (label->wordWrap())
            {
                if (label->heightForWidth(size.width()) > size.height())
                {
                    clipped.append(nameOf(widget) + QStringLiteral(" is shorter than its lines (%1 < %2 at %3)")
                                                        .arg(size.height())
                                                        .arg(label->heightForWidth(size.width()))
                                                        .arg(size.width()));
                }
            }
            else if (size.width() < label->sizeHint().width() || size.height() < label->minimumSizeHint().height())
            {
                clipped.append(nameOf(widget) + QStringLiteral(" is too small for its text (%1 x %2 < %3 x %4)")
                                                    .arg(size.width())
                                                    .arg(size.height())
                                                    .arg(label->sizeHint().width())
                                                    .arg(label->minimumSizeHint().height()));
            }
        }
    }
    return clipped;
}

/// Every scroll area of a window whose content is shorter than it needs for its width - its height follows its width
/// and the area sized it for another - each named with the two heights. Whatever the font, a content laid out for its
/// width is as tall as it says it needs.
[[nodiscard]] inline QStringList unsizedScrollContents(const QWidget& root)
{
    QStringList unsized;
    for (const QScrollArea* area : root.findChildren<QScrollArea*>())
    {
        const QWidget* const content = area->widget();
        if (content == nullptr || !area->isVisibleTo(&root) || !area->widgetResizable() ||
            !content->hasHeightForWidth())
        {
            continue;
        }
        const int needs = content->heightForWidth(content->width());
        if (content->height() < needs)
        {
            unsized.append(nameOf(content) + QStringLiteral(" in ") + nameOf(area) +
                           QStringLiteral(" is %1 px tall where its width needs %2").arg(content->height()).arg(needs));
        }
    }
    return unsized;
}

/// Whether the platform measures text as the interface does: the box font of the suites' platform gives every character
/// one width.
[[nodiscard]] inline bool textIsMeasured()
{
    const QFontMetrics metrics(QApplication::font());
    return metrics.horizontalAdvance(QStringLiteral("iiiiiiiiii")) <
           metrics.horizontalAdvance(QStringLiteral("WWWWWWWWWW"));
}

} // namespace an::tests::look
