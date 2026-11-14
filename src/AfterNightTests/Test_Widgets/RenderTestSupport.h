// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a render case of a component is made of: the looks it runs under - dark, light and a tinted preset, each at
/// both densities, the proxy style installed under the Observatory profile - the four device pixel ratios it paints at,
/// the texts a painting drew with their colours and fonts, and what assistive technology is told of a widget. Expected
/// colours and sizes are read from the tokens in force, so one case covers every look; no picture is stored.

#include "fixtures/ObservatoryLook.h"
#include "fixtures/PaintRecorder.h"
#include "theme/AnDesignTokens.h"

#include <QAccessible>
#include <QAccessibleInterface>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace an::tests::widgets
{

using an::ui::testing::RecordedText;
using an::ui::testing::RenderedPicture;

/// Shows a widget without putting it on a screen, so that a resize reaches it at once - a hidden widget is told of its
/// size only when it is first shown - and its children are laid out as they are in a window.
inline void showOffScreen(QWidget& widget)
{
    widget.setAttribute(Qt::WA_DontShowOnScreen, true);
    widget.show();
}

/// Runs a check under each look of the Observatory profile, the proxy style installed, the look's name traced. A
/// widget a check makes is made under the look it is checked in.
inline void forEachLook(const std::function<void(const an::ui::DesignTokens&)>& check)
{
    an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[0]);
    for (const an::ui::testing::ObservatoryLook& look : an::ui::testing::observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        scope.setLook(look);
        check(an::ui::tokens());
    }
}

/// Renders a widget, with its children, over a background that stands for what it lies on, at each of the four device
/// pixel ratios a render case paints at, and hands each picture to a check.
inline void forEachRatio(QWidget& widget, const QColor& background,
                         const std::function<void(const RenderedPicture&)>& check)
{
    for (const qreal ratio : an::ui::testing::kRenderRatios)
    {
        SCOPED_TRACE(std::string("device pixel ratio ") + std::to_string(ratio));
        check(RenderedPicture::ofWidget(widget, ratio, background));
    }
}

/// The runs of text a widget's painting draws, each with its pen, font and opacity.
inline std::vector<RecordedText> recordedTexts(QWidget& widget)
{
    an::ui::testing::PaintRecorder recorder(widget.size());
    widget.render(&recorder);
    return recorder.texts();
}

/// The first run of text a widget's painting draws that holds `part`, or an empty run with no text.
inline RecordedText recordedText(QWidget& widget, const QString& part)
{
    for (const RecordedText& text : recordedTexts(widget))
    {
        if (text.text.contains(part))
        {
            return text;
        }
    }
    return RecordedText{};
}

/// Every run of text a widget's painting draws, one a line: what a failure says it found.
inline QString describedTexts(QWidget& widget)
{
    QStringList lines;
    for (const RecordedText& text : recordedTexts(widget))
    {
        lines.append(QStringLiteral("\"%1\" in %2").arg(text.text, text.colour.name()));
    }
    return QStringLiteral("painted: ") + lines.join(QStringLiteral("; "));
}

/// Expects a colour within a step per channel of another - what a blend's rounding may move.
inline void expectColour(const QColor& actual, const QColor& expected, const std::string& where,
                         const int tolerance = 2)
{
    EXPECT_TRUE(an::ui::testing::sameColour(actual, expected, tolerance))
        << where << ": " << an::ui::testing::colourName(actual) << ", expected "
        << an::ui::testing::colourName(expected);
}

/// A font's size in logical pixels.
inline int logicalPixels(const QFont& font)
{
    return font.pixelSize() > 0 ? font.pixelSize() : qRound(font.pointSizeF() * 96.0 / 72.0);
}

/// What assistive technology is told a widget is.
inline QAccessible::Role accessibleRole(QWidget& widget)
{
    QAccessibleInterface* const iface = QAccessible::queryAccessibleInterface(&widget);
    return iface != nullptr ? iface->role() : QAccessible::NoRole;
}

/// The name assistive technology reads for a widget.
inline QString accessibleName(QWidget& widget)
{
    QAccessibleInterface* const iface = QAccessible::queryAccessibleInterface(&widget);
    return iface != nullptr ? iface->text(QAccessible::Name) : QString();
}

/// Pumps the event loop until a condition holds or a deadline passes; whether it held.
inline bool pumpUntil(const std::function<bool()>& condition, const int milliseconds = 2000)
{
    const QDeadlineTimer deadline(milliseconds);
    while (!condition())
    {
        if (deadline.hasExpired())
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

/// Pumps the event loop for a span of time, for a case that proves something does not happen meanwhile.
inline void pumpFor(const int milliseconds)
{
    const QDeadlineTimer deadline(milliseconds);
    while (!deadline.hasExpired())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

} // namespace an::tests::widgets
