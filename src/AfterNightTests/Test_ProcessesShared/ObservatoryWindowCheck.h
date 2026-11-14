// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A family's process windows opened under the Observatory profile - the developer switch's look - and held to the
/// window AfterNight 1.1 shows: no control lies outside the widget that holds it, no text is cut that the 1.1 window
/// shows whole, at either density; and grabbed with Night on, no pixel holds any green or blue. A primitive of the base
/// style that lays a white or a black shade is the one way a hue survives Night, so the grab is where one is found.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageViewControl.h"
#include "fixtures/ObservatoryLook.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace an::tests::processes::shared
{

/// What a look of a window shows that a case holds it to, each widget named by its path in the window.
struct WindowLook
{
    /// Widgets that lie, wholly or in part, outside the widget that holds them.
    QStringList outsideTheirParent;
    /// Labels and buttons narrower than their text: the text is cut.
    QSet<QString> cutText;
};

/// A widget's path in its window: each widget up from it by its object name, or its class and its place among its
/// siblings of that class.
[[nodiscard]] inline QString widgetPath(const QWidget& widget, const QWidget& window)
{
    QStringList parts;
    for (const QWidget* at = &widget; at != nullptr && at != &window; at = at->parentWidget())
    {
        QString part = at->objectName();
        if (part.isEmpty())
        {
            int index = 0;
            if (const QWidget* parent = at->parentWidget())
            {
                for (const QObject* sibling : parent->children())
                {
                    if (sibling == at)
                    {
                        break;
                    }
                    index += sibling->metaObject() == at->metaObject() ? 1 : 0;
                }
            }
            part = QStringLiteral("%1#%2").arg(QString::fromLatin1(at->metaObject()->className())).arg(index);
        }
        parts.prepend(part);
    }
    return parts.join(QLatin1Char('/'));
}

/// Whether a widget lies inside a scroll area's viewport, where what it holds may lie past the viewport by design.
[[nodiscard]] inline bool insideAScrolledViewport(const QWidget& widget, const QWidget& window)
{
    for (const QWidget* at = widget.parentWidget(); at != nullptr && at != &window; at = at->parentWidget())
    {
        if (const auto* area = qobject_cast<const QAbstractScrollArea*>(at->parentWidget()))
        {
            if (area->viewport() == at)
            {
                return true;
            }
        }
    }
    return false;
}

/// How wide a button's text is as the style paints it: semibold under the Observatory profile, at the button's own font
/// under the Classic one, with its icon beside it.
[[nodiscard]] inline int paintedTextWidth(const QAbstractButton& button)
{
    QFont font = button.font();
    if (an::ui::styleProfile() == an::ui::StyleProfile::Observatory)
    {
        font.setWeight(QFont::DemiBold);
    }
    QString text = button.text();
    text.remove(QLatin1Char('&'));
    int width = QFontMetrics(font).horizontalAdvance(text);
    if (!button.icon().isNull())
    {
        width += button.iconSize().width() + 4;
    }
    return width;
}

/// What a shown window's look holds a case to.
[[nodiscard]] inline WindowLook inspectWindow(QWidget& window)
{
    WindowLook look;
    for (QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->isWindow() || !widget->isVisibleTo(&window) || widget->size().isEmpty())
        {
            continue;
        }
        const QWidget* const parent = widget->parentWidget();
        const bool scrolled = insideAScrolledViewport(*widget, window);
        if (parent != nullptr && !parent->size().isEmpty() && !scrolled &&
            !QRect(QPoint(0, 0), parent->size()).contains(widget->geometry()))
        {
            look.outsideTheirParent.append(QStringLiteral("%1 at (%2, %3, %4 x %5) in %6 x %7")
                                               .arg(widgetPath(*widget, window))
                                               .arg(widget->x())
                                               .arg(widget->y())
                                               .arg(widget->width())
                                               .arg(widget->height())
                                               .arg(parent->width())
                                               .arg(parent->height()));
        }
        if (scrolled)
        {
            continue;
        }
        if (const auto* label = qobject_cast<const QLabel*>(widget))
        {
            if (label->text().isEmpty())
            {
                continue;
            }
            const bool cut = label->wordWrap() ? label->height() < label->heightForWidth(label->width())
                                               : label->width() < label->minimumSizeHint().width();
            if (cut)
            {
                look.cutText.insert(widgetPath(*widget, window));
            }
        }
        else if (const auto* button = qobject_cast<const QAbstractButton*>(widget))
        {
            if (button->text().isEmpty())
            {
                continue;
            }
            const bool indicator = qobject_cast<const QCheckBox*>(button) || qobject_cast<const QRadioButton*>(button);
            const bool cut = indicator ? button->width() < button->minimumSizeHint().width()
                                       : button->width() < paintedTextWidth(*button) + 2;
            if (cut)
            {
                look.cutText.insert(widgetPath(*widget, window));
            }
        }
    }
    return look;
}

/// How many pixels of a grab hold green or blue, and where the first few are, with the widget painted there.
struct ColouredPixels
{
    int pixels = 0;
    QStringList where;
};

[[nodiscard]] inline ColouredPixels colouredPixels(QWidget& window, const QImage& grabbed)
{
    ColouredPixels coloured;
    const QImage pixels = grabbed.convertToFormat(QImage::Format_ARGB32);
    const qreal ratio = pixels.devicePixelRatio();
    for (int y = 0; y < pixels.height(); ++y)
    {
        for (int x = 0; x < pixels.width(); ++x)
        {
            const QRgb pixel = pixels.pixel(x, y);
            if (qGreen(pixel) == 0 && qBlue(pixel) == 0)
            {
                continue;
            }
            ++coloured.pixels;
            if (coloured.where.size() < 12)
            {
                const QPoint logical(static_cast<int>(x / ratio), static_cast<int>(y / ratio));
                const QWidget* const under = window.childAt(logical);
                coloured.where.append(
                    QStringLiteral("(%1, %2) #%3 in %4")
                        .arg(logical.x())
                        .arg(logical.y())
                        .arg(pixel & 0xFFFFFFu, 6, 16, QLatin1Char('0'))
                        .arg(under != nullptr ? widgetPath(*under, window) : QStringLiteral("window")));
            }
        }
    }
    return coloured;
}

/// Launches a registered descriptor over a target view as the menu does, and answers the window it showed; fails the
/// case and answers null when none opened.
[[nodiscard]] inline QWidget* launchWindowOf(const std::string& descriptorId, ImageViewControl& target)
{
    const an::ui::process_registry::ProcessDescriptor* descriptor = registered(descriptorId);
    EXPECT_NE(descriptor, nullptr) << descriptorId;
    if (descriptor == nullptr)
    {
        return nullptr;
    }
    if (descriptor->launch_single_image)
    {
        descriptor->launch_single_image(&target);
    }
    else if (descriptor->launch_top_level)
    {
        descriptor->launch_top_level();
    }
    pumpQuietly();
    const std::vector<OpenedProcessWindow> opened = openedProcessWindows();
    EXPECT_EQ(opened.size(), 1u) << descriptorId << " opened " << opened.size() << " windows";
    return opened.empty() ? nullptr : opened.front().window;
}

/// Opens each descriptor's window over the target in the 1.1 look, then under the Observatory profile at each density,
/// and holds every Observatory window to the 1.1 one; grabs it with Night on and holds it to having no hue.
inline void expectObservatoryWindowsHold(const std::vector<std::string>& descriptorIds, ImageViewControl& target)
{
    using an::ui::testing::ObservatoryLook;
    using an::ui::testing::observatoryLooks;
    using an::ui::testing::ObservatoryStyleScope;
    const ObservatoryLook& compact = observatoryLooks()[0];
    const ObservatoryLook& comfortable = observatoryLooks()[3];
    for (const std::string& id : descriptorIds)
    {
        SCOPED_TRACE(id);
        WindowLook classic;
        {
            const ObservatoryStyleScope scope(compact, an::ui::StyleProfile::Classic);
            QWidget* const window = launchWindowOf(id, target);
            if (window == nullptr)
            {
                continue;
            }
            classic = inspectWindow(*window);
            closeOpenedProcessWindows();
        }
        for (const ObservatoryLook* look : {&compact, &comfortable})
        {
            SCOPED_TRACE(look->name);
            ObservatoryStyleScope scope(*look);
            QWidget* const window = launchWindowOf(id, target);
            if (window == nullptr)
            {
                break;
            }
            const WindowLook observatory = inspectWindow(*window);
            EXPECT_TRUE(observatory.outsideTheirParent.isEmpty())
                << "outside their parent: " << observatory.outsideTheirParent.join(QStringLiteral("; ")).toStdString();
            QStringList newlyCut;
            for (const QString& path : observatory.cutText)
            {
                if (!classic.cutText.contains(path))
                {
                    newlyCut.append(path);
                }
            }
            EXPECT_TRUE(newlyCut.isEmpty())
                << "text cut that the 1.1 window shows whole: " << newlyCut.join(QStringLiteral("; ")).toStdString();
            if (look == &compact)
            {
                // Night switched on as the application switches it for an open window: the style's tokens, then the
                // window's own refresh, which sets its icons and pictures again.
                scope.setNight(true);
                if (auto* process = dynamic_cast<TopLevelProcessBaseControl*>(window))
                {
                    process->refreshTheme();
                }
                pumpQuietly();
                const ColouredPixels coloured = colouredPixels(*window, window->grab().toImage());
                EXPECT_EQ(coloured.pixels, 0) << "the window keeps a hue under Night at "
                                              << coloured.where.join(QStringLiteral("; ")).toStdString();
                scope.setNight(false);
            }
            closeOpenedProcessWindows();
        }
    }
}

} // namespace an::tests::processes::shared
