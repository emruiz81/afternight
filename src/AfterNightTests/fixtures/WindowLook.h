// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a window that stays a window is held to, whichever window it is: a heading at its head that says its title; a
/// footer whose rightmost button is the primary one; text that meets the contrast rule against the surface it is
/// painted on; no child outside its parent and no control smaller than it needs, as it opens and at its least size; no
/// label cut short as it opens; and a window no larger than the available area of its screen, however large it was
/// asked to be.
///
/// `expectWindowLook` runs it all over a window a case makes as the product makes it, in both densities. What depends
/// on how wide text is drawn, and on a screen that can hold the window, is measured where the platform measures it -
/// natively (`QT_QPA_PLATFORM=windows`) - and reported skipped under the suites' own platform, whose font is a box font
/// and whose screen is 240 x 320.

#pragma once

#include "fixtures/ClippedControls.h"
#include "fixtures/ObservatoryLook.h"

#include "components/AnCardWidget.h"
#include "components/AnElidedLabel.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDialog>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QList>
#include <QMargins>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>

namespace an::tests::look
{

/// Lets a window settle: posted work delivered, the deletions done, the layouts applied and the sizing a window does a
/// moment after it is shown - a process window fits its content again thirty milliseconds on.
inline void settleWindow(const int milliseconds = 90)
{
    const QDeadlineTimer deadline(milliseconds);
    do
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (!deadline.hasExpired());
}

/// Where a widget lies in its window.
[[nodiscard]] inline QRect placeIn(const QWidget& window, const QWidget* widget)
{
    return QRect(widget->mapTo(&window, QPoint(0, 0)), widget->size());
}

/// A window's heading: the heading widget of a window that stays a window, or the header of a process's window, which
/// is one in all but its widget. Null when the window shows neither.
[[nodiscard]] inline const QWidget* headingOf(const QWidget& window)
{
    for (const char* const name : {"anWindowHeading", "anProcessShellHeader"})
    {
        for (const QWidget* candidate : window.findChildren<QWidget*>(QString::fromLatin1(name)))
        {
            if (candidate->isVisibleTo(&window))
            {
                return candidate;
            }
        }
    }
    return nullptr;
}

/// The title a heading says.
[[nodiscard]] inline QString headingTitle(const QWidget& heading)
{
    for (const char* const name : {"anWindowHeadingTitle", "anProcessShellHeaderTitle"})
    {
        if (const auto* const title = heading.findChild<QLabel*>(QString::fromLatin1(name)))
        {
            return title->text();
        }
    }
    return {};
}

/// What is wrong with a window's heading: it has none, its heading says no title, or something of the window stands
/// above it.
[[nodiscard]] inline QStringList headingProblems(const QWidget& window)
{
    const QWidget* const heading = headingOf(window);
    if (heading == nullptr)
    {
        return {QStringLiteral("the window shows no heading")};
    }
    QStringList problems;
    if (headingTitle(*heading).trimmed().isEmpty())
    {
        problems.append(QStringLiteral("the window's heading says no title"));
    }
    const int top = placeIn(window, heading).top();
    for (const QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->isWindow() || !widget->isVisibleTo(&window) || widget == heading || heading->isAncestorOf(widget) ||
            widget->isAncestorOf(heading))
        {
            continue;
        }
        // What stands above the heading: a part with something of its own to show, wholly over the heading's top.
        const bool shows = qobject_cast<const QLabel*>(widget) != nullptr ||
                           qobject_cast<const QAbstractButton*>(widget) != nullptr ||
                           qobject_cast<const QAbstractScrollArea*>(widget) != nullptr;
        if (shows && widget->height() > 0 && placeIn(window, widget).bottom() < top)
        {
            problems.append(nameOf(widget) + QStringLiteral(" stands above the heading"));
        }
    }
    return problems;
}

/// Whether something of a widget can be seen in its window: shown, and not scrolled out of the area that holds it.
[[nodiscard]] inline bool isSeenIn(const QWidget& window, const QWidget* widget)
{
    return widget->isVisibleTo(&window) && !widget->visibleRegion().isEmpty();
}

/// Whether a button is drawn as the primary one.
[[nodiscard]] inline bool isPrimary(const QAbstractButton& button)
{
    return button.property("buttonVariant").toString() == QLatin1String("primary") ||
           button.property("anButtonVariant").toString() == QLatin1String("primary") ||
           button.property("anProcessButtonRole").toString() == QLatin1String("primary");
}

/// The buttons of a window's footer, from left to right: the lowest row of push buttons, where nothing of the window
/// lies under it. Empty for a window with no footer of buttons - a viewer whose only buttons are among its content.
[[nodiscard]] inline QList<const QPushButton*> footerButtons(const QWidget& window)
{
    QList<const QPushButton*> shown;
    for (const QPushButton* button : window.findChildren<QPushButton*>())
    {
        if (isSeenIn(window, button) && button->window() == &window)
        {
            shown.append(button);
        }
    }
    if (shown.isEmpty())
    {
        return {};
    }
    const QPushButton* lowest = shown.front();
    for (const QPushButton* button : shown)
    {
        if (placeIn(window, button).bottom() > placeIn(window, lowest).bottom())
        {
            lowest = button;
        }
    }
    const QRect band = placeIn(window, lowest);
    // Something of the window under the row: the row is among the content, not at the window's foot.
    for (const QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->isWindow() || !isSeenIn(window, widget) || widget->height() == 0)
        {
            continue;
        }
        const bool shows = qobject_cast<const QLabel*>(widget) != nullptr ||
                           qobject_cast<const QAbstractButton*>(widget) != nullptr ||
                           qobject_cast<const QAbstractItemView*>(widget) != nullptr;
        if (shows && placeIn(window, widget).top() > band.bottom())
        {
            return {};
        }
    }
    QList<const QPushButton*> row;
    for (const QPushButton* button : shown)
    {
        const int centre = placeIn(window, button).center().y();
        if (centre >= band.top() && centre <= band.bottom())
        {
            row.append(button);
        }
    }
    std::sort(row.begin(), row.end(), [&window](const QPushButton* left, const QPushButton* right) {
        return placeIn(window, left).left() < placeIn(window, right).left();
    });
    return row;
}

/// What is wrong with a window's footer: its rightmost button is not the primary one.
[[nodiscard]] inline QStringList footerProblems(const QWidget& window)
{
    const QList<const QPushButton*> row = footerButtons(window);
    if (row.isEmpty())
    {
        return {};
    }
    if (!isPrimary(*row.back()))
    {
        QStringList texts;
        for (const QPushButton* button : row)
        {
            texts.append(QStringLiteral("\"%1\"%2")
                             .arg(button->text(), isPrimary(*button) ? QStringLiteral(" (primary)") : QString()));
        }
        return {QStringLiteral("the footer's rightmost button is not the primary one: ") +
                texts.join(QStringLiteral(", "))};
    }
    return {};
}

/// The surface a widget's text is painted on: the fill of the nearest part around it that paints one - a card, an item
/// view or a field's base, a part that fills its background - and the window's otherwise.
[[nodiscard]] inline QColor surfaceUnder(const QWidget& window, const QWidget* widget)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    for (const QWidget* around = widget->parentWidget(); around != nullptr; around = around->parentWidget())
    {
        if (dynamic_cast<const an::ui::AnCardWidget*>(around) != nullptr ||
            around->objectName() == QLatin1String("anProcessShellCard"))
        {
            return t.colors.bgCardPrimary;
        }
        if (const auto* const group = qobject_cast<const QGroupBox*>(around);
            group != nullptr && !group->isFlat() &&
            group->property("anGroupVariant").toString() != QLatin1String("flat"))
        {
            return t.colors.bgCard;
        }
        if (const auto* const view = qobject_cast<const QAbstractItemView*>(around->parentWidget());
            view != nullptr && view->viewport() == around)
        {
            return around->palette().color(QPalette::Base);
        }
        if (around->autoFillBackground() && around->backgroundRole() != QPalette::NoRole)
        {
            const QColor fill = around->palette().color(around->backgroundRole());
            if (fill.alpha() == 255)
            {
                return fill;
            }
        }
        if (around == &window)
        {
            break;
        }
    }
    return window.palette().color(QPalette::Window);
}

/// Every text of a window that does not meet the contrast rule against the surface it is painted on, each named with
/// its colours and its ratio: 4.5 to 1 for text, and 3 to 1 for the tertiary text and a status colour, which the theme
/// promises no more of. The text's colour is read from the widget's palette, in the role it draws its text in. A
/// disabled widget is left out: it is drawn dimmed by design.
[[nodiscard]] inline QStringList contrastProblems(const QWidget& window)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QStringList problems;
    for (const QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->isWindow() || !widget->isVisibleTo(&window) || !widget->isEnabled())
        {
            continue;
        }
        bool text = false;
        if (const auto* const label = qobject_cast<const QLabel*>(widget))
        {
            text = !label->text().trimmed().isEmpty() && label->pixmap().isNull();
        }
        else if (qobject_cast<const QCheckBox*>(widget) != nullptr ||
                 qobject_cast<const QRadioButton*>(widget) != nullptr)
        {
            text = !static_cast<const QAbstractButton*>(widget)->text().isEmpty();
        }
        else if (const auto* const group = qobject_cast<const QGroupBox*>(widget))
        {
            text = !group->title().isEmpty();
        }
        if (!text)
        {
            continue;
        }
        const QColor surface = surfaceUnder(window, widget);
        const QColor colour = an::ui::compositeOver(widget->palette().color(widget->foregroundRole()), surface);
        const QColor asked = widget->palette().color(widget->foregroundRole());
        const bool lesser = asked == t.colors.textTertiary || asked == t.colors.statusSuccess ||
                            asked == t.colors.statusWarning || asked == t.colors.statusError ||
                            asked == t.colors.statusInfo;
        const double needed = lesser ? 3.0 : 4.5;
        const double ratio = an::ui::contrastRatio(colour, surface);
        if (ratio + 1e-6 < needed)
        {
            problems.append(nameOf(widget) + QStringLiteral(": %1 on %2 is %3 to 1, under %4")
                                                 .arg(colour.name(), surface.name())
                                                 .arg(ratio, 0, 'f', 2)
                                                 .arg(needed, 0, 'f', 1));
        }
    }
    return problems;
}

/// The labels of a window that leave some of their text out, each by its name and its whole text.
[[nodiscard]] inline QStringList elidedLabels(const QWidget& window)
{
    QStringList elided;
    for (const QLabel* label : window.findChildren<QLabel*>())
    {
        const auto* const fitted = dynamic_cast<const an::ui::AnElidedLabel*>(label);
        if (fitted != nullptr && fitted->isVisibleTo(&window) && fitted->isElided())
        {
            elided.append(fitted->objectName() + QStringLiteral(": ") + fitted->fullText());
        }
    }
    return elided;
}

/// The margins the window system draws round a window.
[[nodiscard]] inline QMargins frameOf(const QWidget& window)
{
    const QRect frame = window.frameGeometry();
    const QRect inner = window.geometry();
    return {inner.left() - frame.left(), inner.top() - frame.top(), frame.right() - inner.right(),
            frame.bottom() - inner.bottom()};
}

/// Whether the screen a window is on can hold it at its least size, its frame included: a screen that cannot bounds
/// nothing.
[[nodiscard]] inline bool screenHolds(const QWidget& window)
{
    const QScreen* const screen = window.screen();
    if (screen == nullptr)
    {
        return false;
    }
    const QMargins frame = frameOf(window);
    const QRect area = screen->availableGeometry();
    return area.width() >= window.minimumWidth() + frame.left() + frame.right() &&
           area.height() >= window.minimumHeight() + frame.top() + frame.bottom();
}

/// Says where a window stands out of its screen's available area; empty when it lies within it.
[[nodiscard]] inline QString standsOutOfItsScreen(const QWidget& window)
{
    const QRect area = window.screen()->availableGeometry();
    const QRect frame = window.frameGeometry();
    if (area.contains(frame))
    {
        return {};
    }
    return QStringLiteral("the window's frame (%1, %2, %3 x %4) stands out of the screen's area (%5, %6, %7 x %8)")
        .arg(frame.x())
        .arg(frame.y())
        .arg(frame.width())
        .arg(frame.height())
        .arg(area.x())
        .arg(area.y())
        .arg(area.width())
        .arg(area.height());
}

/// Makes a window as the product makes it, not yet shown; the window is the case's until it is let go.
using MakeWindow = std::function<QWidget*()>;

/// Lets a window a case made go.
using LetWindowGo = std::function<void(QWidget*)>;

/// Closes a window a case made and lets it go: one that deletes itself as it closes is gone with its close, another is
/// deleted.
inline void letGo(QWidget* window)
{
    const QPointer<QWidget> held(window);
    if (!held.isNull())
    {
        held->close();
    }
    settleWindow(20);
    if (!held.isNull())
    {
        delete held.data();
    }
    settleWindow(20);
}

/// Shows a window without putting it on a screen, and lets it settle; a window the product has shown already is
/// left where it is.
inline void showOffTheScreen(QWidget& window)
{
    if (!window.isVisible())
    {
        window.setAttribute(Qt::WA_DontShowOnScreen, true);
        window.show();
    }
    settleWindow();
}

/// The room the floor display leaves a window: 1280 x 720 - a 1920 x 1080 panel at 150 % - less its taskbar and the
/// window's frame. A window whose least size fits it is reached whole on every display the interface is built for.
inline constexpr QSize kFloorRoom{1278, 656};

/// Says a size as a failure and a report say it.
[[nodiscard]] inline std::string sizeText(const QSize& size)
{
    return std::to_string(size.width()) + " x " + std::to_string(size.height());
}

/// What a run of `expectWindowLook` could not measure under the platform it ran on.
struct WindowLookMeasured
{
    bool text = true;
    bool screen = true;
};

/// Holds a shown window to the look, in the density the scope's look states: a heading and a footer as the look says;
/// in the dark theme, the light one and a tinted preset, text that meets the contrast rule; nothing of it clipped as it
/// opens or at its least size; no label cut short as it opens; and the window within its screen's available area.
/// `first` is the density's dark look among `observatoryLooks()`; the scope is left in it.
inline void checkShownWindow(QWidget& window, an::ui::testing::ObservatoryStyleScope& scope, const std::size_t first,
                             WindowLookMeasured& measured)
{
    const auto& looks = an::ui::testing::observatoryLooks();
    measured.text = measured.text && textIsMeasured();

    const QStringList heading = headingProblems(window);
    EXPECT_TRUE(heading.isEmpty()) << heading.join(QStringLiteral("\n  ")).toStdString();
    const QStringList footer = footerProblems(window);
    EXPECT_TRUE(footer.isEmpty()) << footer.join(QStringLiteral("\n  ")).toStdString();
    const QStringList unsized = unsizedScrollContents(window);
    EXPECT_TRUE(unsized.isEmpty()) << unsized.join(QStringLiteral("\n  ")).toStdString();
    for (const std::size_t theme : {first, first + 1u, first + 2u})
    {
        if (theme != first)
        {
            scope.setLook(looks[theme]);
            settleWindow(10);
        }
        const QStringList contrast = contrastProblems(window);
        EXPECT_TRUE(contrast.isEmpty()) << looks[theme].name << ": "
                                        << contrast.join(QStringLiteral("\n  ")).toStdString();
    }
    scope.setLook(looks[first]);
    settleWindow(10);

    if (textIsMeasured())
    {
        const QStringList opened = clippedControls(window);
        EXPECT_TRUE(opened.isEmpty()) << "as it opens, " << window.width() << " x " << window.height() << ":\n  "
                                      << opened.join(QStringLiteral("\n  ")).toStdString();
        const QStringList cut = elidedLabels(window);
        EXPECT_TRUE(cut.isEmpty()) << "cut short as it opens:\n  " << cut.join(QStringLiteral("\n  ")).toStdString();
    }
    if (screenHolds(window))
    {
        const QString out = standsOutOfItsScreen(window);
        EXPECT_TRUE(out.isEmpty()) << "as it opens: " << out.toStdString();
    }
    else
    {
        measured.screen = false;
    }
    if (textIsMeasured())
    {
        const std::string density = looks[first].density == an::ui::ThemeDensity::Compact ? "compact" : "comfortable";
        ::testing::Test::RecordProperty(density + "_opens", sizeText(window.size()));
        QStringList footerTexts;
        for (const QPushButton* button : footerButtons(window))
        {
            footerTexts.append(button->text());
        }
        ::testing::Test::RecordProperty(density + "_footer", footerTexts.join(QStringLiteral(" | ")).toStdString());
        // At its least size: asked to be smaller than it can be, a window takes the least size it has.
        window.resize(1, 1);
        settleWindow();
        ::testing::Test::RecordProperty(density + "_least", sizeText(window.size()));
        const QStringList least = clippedControls(window);
        EXPECT_TRUE(least.isEmpty()) << "at its least size, " << window.width() << " x " << window.height() << ":\n  "
                                     << least.join(QStringLiteral("\n  ")).toStdString();
        // The least size fits the floor display, so the window is held within it and reached whole there.
        EXPECT_TRUE(window.width() <= kFloorRoom.width() && window.height() <= kFloorRoom.height())
            << "at its least size, " << sizeText(window.size()) << ", the window does not fit the room a 1280 x 720 "
            << "display leaves it, " << sizeText(kFloorRoom);
    }
}

/// Holds a window to the look in both densities. In each, a window made as the product makes it is held as
/// `checkShownWindow` says - and a second one, asked before its first show to be larger than the screen, opens within
/// the screen's available area all the same. A window the product shows as it makes it is taken as it comes: it cannot
/// be asked to be larger first.
inline WindowLookMeasured expectWindowLook(const MakeWindow& make, const LetWindowGo& dispose = letGo)
{
    WindowLookMeasured measured;
    using an::ui::testing::observatoryLooks;
    using an::ui::testing::ObservatoryStyleScope;

    const auto& looks = observatoryLooks();
    // Dark, light and tinted, in the compact density and then in the comfortable one.
    for (const std::size_t first : {std::size_t{0}, std::size_t{3}})
    {
        ObservatoryStyleScope scope(looks[first]);
        settleWindow(10);
        const std::string density = looks[first].name;
        SCOPED_TRACE(density);

        QWidget* const window = make();
        EXPECT_NE(window, nullptr);
        if (window == nullptr)
        {
            return measured;
        }
        showOffTheScreen(*window);
        checkShownWindow(*window, scope, first, measured);
        dispose(window);

        QWidget* const large = make();
        EXPECT_NE(large, nullptr);
        if (large == nullptr)
        {
            return measured;
        }
        if (!large->isVisible())
        {
            if (const QScreen* const screen = QGuiApplication::primaryScreen())
            {
                const QRect area = screen->availableGeometry();
                large->resize(area.width() + 240, area.height() + 240);
            }
            showOffTheScreen(*large);
            if (screenHolds(*large))
            {
                const QString out = standsOutOfItsScreen(*large);
                EXPECT_TRUE(out.isEmpty()) << "asked to be larger than its screen: " << out.toStdString();
            }
            else
            {
                measured.screen = false;
            }
        }
        dispose(large);
    }
    return measured;
}

/// Runs the product's own call that shows a window in a loop of its own - a dialog it runs to its answer - and returns
/// when the window is gone.
using RunModalWindow = std::function<void()>;

/// Says whether a window shown in a loop of its own is the one a case means.
using IsTheWindow = std::function<bool(const QWidget&)>;

/// Holds a window the product runs in a loop of its own to the look, in both densities: the window is looked at from
/// inside its loop as `checkShownWindow` says, and then dismissed. `run` shows it, and returns once it is gone; `isIt`
/// tells it from any other window. A case fails where `run` shows no such window.
inline WindowLookMeasured expectModalWindowLook(const RunModalWindow& run, const IsTheWindow& isIt)
{
    WindowLookMeasured measured;
    using an::ui::testing::observatoryLooks;
    using an::ui::testing::ObservatoryStyleScope;

    // Looks at the window from the loop it runs, once, and dismisses it; whether it was seen.
    const auto inItsLoop = [&run, &isIt](const std::function<void(QWidget&)>& look) {
        bool seen = false;
        QTimer poll;
        poll.setInterval(10);
        int attempts = 0;
        QObject::connect(&poll, &QTimer::timeout, &poll, [&]() {
            QWidget* const modal = QApplication::activeModalWidget();
            if (modal != nullptr && modal->isVisible() && isIt(*modal))
            {
                poll.stop();
                seen = true;
                settleWindow();
                look(*modal);
                if (auto* const dialog = qobject_cast<QDialog*>(modal))
                {
                    dialog->reject();
                }
                else
                {
                    modal->close();
                }
            }
            else if (++attempts > 1000)
            {
                // No such window came: whatever loop is running is ended, so the case fails and does not hang.
                poll.stop();
                if (modal != nullptr)
                {
                    modal->close();
                }
            }
        });
        poll.start();
        run();
        poll.stop();
        return seen;
    };

    const auto& looks = observatoryLooks();
    for (const std::size_t first : {std::size_t{0}, std::size_t{3}})
    {
        ObservatoryStyleScope scope(looks[first]);
        settleWindow(10);
        const std::string density = looks[first].name;
        SCOPED_TRACE(density);
        EXPECT_TRUE(inItsLoop([&](QWidget& window) { checkShownWindow(window, scope, first, measured); }))
            << "the window was not shown";
        settleWindow(20);
    }
    return measured;
}

/// What a case that ran `expectWindowLook` reports of what its platform could not measure; empty when all of it was.
[[nodiscard]] inline std::string notMeasured(const WindowLookMeasured& measured)
{
    std::string said;
    if (!measured.text)
    {
        said +=
            "whether a control is clipped or a label cut short is measured under the native platform, where text is "
            "as wide as the interface draws it";
    }
    if (!measured.screen)
    {
        said += said.empty() ? "" : "; ";
        said += "the screen cannot hold the window's least size, and so bounds nothing";
    }
    return said;
}

} // namespace an::tests::look

/// Ends a case that ran `expectWindowLook`: reported skipped, saying what, where its platform could not measure all of
/// it and nothing it did measure failed.
#define AN_WINDOW_LOOK_REPORT(measured)                                                                   \
    do                                                                                                    \
    {                                                                                                     \
        const std::string anWindowLookNotMeasured = an::tests::look::notMeasured(measured);               \
        if (!anWindowLookNotMeasured.empty() && !::testing::Test::HasFailure())                           \
        {                                                                                                 \
            GTEST_SKIP() << "the heading, the footer and the contrast hold; " << anWindowLookNotMeasured; \
        }                                                                                                 \
    } while (false)
