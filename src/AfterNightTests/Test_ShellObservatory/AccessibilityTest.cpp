// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Observatory window as assistive technology and the keyboard meet it, on every page a person works on - Home,
/// Project Home, the Integration hub, a workspace, a task's page of each surface and the full pages: every interactive
/// widget has an accessible name and a role, and Tab visits every control once, in the page's reading order, and comes
/// back to where it began.

#include "ObservatoryPages.h"

#include "chrome/TopBar.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRect>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QTextEdit>
#include <QWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace an::tests::observatory
{
namespace
{

/// A widget as a failure names it: its class, its object name and its text, and its path of named parents.
[[nodiscard]] QString describe(const QWidget* widget)
{
    QString text = QString::fromLatin1(widget->metaObject()->className());
    if (!widget->objectName().isEmpty())
    {
        text += QStringLiteral(" '%1'").arg(widget->objectName());
    }
    if (const auto* button = qobject_cast<const QAbstractButton*>(widget);
        button != nullptr && !button->text().isEmpty())
    {
        text += QStringLiteral(" \"%1\"").arg(button->text());
    }
    QStringList parents;
    for (const QWidget* parent = widget->parentWidget(); parent != nullptr && parents.size() < 4;
         parent = parent->parentWidget())
    {
        if (!parent->objectName().isEmpty())
        {
            parents.append(parent->objectName());
        }
    }
    if (!parents.isEmpty())
    {
        text += QStringLiteral(" in ") + parents.join(QStringLiteral(" < "));
    }
    return text;
}

/// Whether a widget is part of a control that lays it out inside itself - a spin box's or a combo box's text, a field's
/// clear button, an item view's or a text area's own parts, a scroll area's bars - and is judged with that control.
[[nodiscard]] bool isPartOfAControl(const QWidget* widget)
{
    if (qobject_cast<const QScrollBar*>(widget) != nullptr)
    {
        return true;
    }
    for (const QWidget* parent = widget->parentWidget(); parent != nullptr; parent = parent->parentWidget())
    {
        if (qobject_cast<const QAbstractSpinBox*>(parent) != nullptr ||
            qobject_cast<const QComboBox*>(parent) != nullptr || qobject_cast<const QLineEdit*>(parent) != nullptr ||
            qobject_cast<const QAbstractItemView*>(parent) != nullptr ||
            qobject_cast<const QTextEdit*>(parent) != nullptr || qobject_cast<const QPlainTextEdit*>(parent) != nullptr)
        {
            return true;
        }
    }
    return false;
}

/// Whether a widget hands the focus it is given to another, which is the stop and is judged in its place.
[[nodiscard]] bool handsItsFocusOn(const QWidget* widget)
{
    return widget->focusProxy() != nullptr;
}

/// Whether a widget of the window takes the focus from Tab now: shown, enabled, a widget of the window itself.
[[nodiscard]] bool takesTab(const QWidget* widget, const QWidget& window)
{
    return widget != nullptr && widget->window() == &window && widget->isVisibleTo(&window) && widget->isEnabled() &&
           (widget->focusPolicy() & Qt::TabFocus) != 0 && !isPartOfAControl(widget) && !handsItsFocusOn(widget);
}

/// The interactive widgets of the window as it is shown: every shown widget that takes the focus from the keyboard, and
/// every shown button, whether or not it takes the focus.
[[nodiscard]] std::vector<QWidget*> interactiveWidgets(QWidget& window)
{
    std::vector<QWidget*> found;
    for (QWidget* widget : window.findChildren<QWidget*>())
    {
        if (widget->window() != &window || !widget->isVisibleTo(&window) || isPartOfAControl(widget) ||
            handsItsFocusOn(widget))
        {
            continue;
        }
        if ((widget->focusPolicy() & Qt::TabFocus) != 0 || qobject_cast<QAbstractButton*>(widget) != nullptr)
        {
            found.push_back(widget);
        }
    }
    return found;
}

/// What is missing of a widget's accessible name and role; empty when it has both.
[[nodiscard]] QString whatIsMissing(QWidget* widget)
{
    QAccessibleInterface* const accessible = QAccessible::queryAccessibleInterface(widget);
    if (accessible == nullptr)
    {
        return QStringLiteral("no accessible interface");
    }
    QStringList missing;
    if (accessible->text(QAccessible::Name).trimmed().isEmpty())
    {
        missing.append(QStringLiteral("no name"));
    }
    const QAccessible::Role role = accessible->role();
    if (role == QAccessible::NoRole || role == QAccessible::Client)
    {
        missing.append(QStringLiteral("no role"));
    }
    return missing.join(QStringLiteral(", "));
}

/// Where a widget lies in the window.
[[nodiscard]] QRect placeOf(const QWidget* widget)
{
    return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
}

/// Whether going from one control to the next goes back in the page's reading order - up to a control wholly above
/// the one before and not wholly at its right (a column read to its foot goes up to the next column, at its right);
/// or, on the same line - each one's middle within the other's height - to a control wholly at its left.
[[nodiscard]] bool goesBack(const QRect& from, const QRect& to)
{
    const bool whollyAbove = to.bottom() < from.top();
    const bool whollyRight = to.left() > from.right();
    const bool sameLine = from.top() <= to.center().y() && to.center().y() <= from.bottom() &&
                          to.top() <= from.center().y() && from.center().y() <= to.bottom();
    const bool whollyLeft = to.right() < from.left();
    return (whollyAbove && !whollyRight) || (sameLine && whollyLeft);
}

class Accessibility : public PagesCase
{
protected:
    void SetUp() override
    {
        PagesCase::SetUp();
        if (!an::tests::app::activateWindow(window()))
        {
            GTEST_SKIP() << "the platform gives the window no activation, so no widget of it has the focus";
        }
    }
};

// Every interactive widget of each page - its own and the window's chrome around it - has an accessible name and a
// role assistive technology tells it by.
TEST_F(Accessibility, EveryInteractiveWidgetHasANameAndARole)
{
    for (const Page page : everyPage())
    {
        SCOPED_TRACE(pageName(page));
        ASSERT_NE(open(page), nullptr);
        QStringList unnamed;
        for (QWidget* widget : interactiveWidgets(window()))
        {
            if (const QString missing = whatIsMissing(widget); !missing.isEmpty())
            {
                unnamed.append(describe(widget) + QStringLiteral(": ") + missing);
            }
        }
        EXPECT_TRUE(unnamed.isEmpty()) << pageName(page) << ", " << unnamed.size() << " widgets:\n  "
                                       << unnamed.join(QStringLiteral("\n  ")).toStdString();
        leave();
    }
}

// Tab visits every control of each page once, in its reading order, and comes back to where it began.
TEST_F(Accessibility, TabOrderVisitsEveryControlOnce)
{
    for (const Page page : everyPage())
    {
        SCOPED_TRACE(pageName(page));
        ASSERT_NE(open(page), nullptr);
        // From the first control a person reads: the top bar's at its left.
        QWidget* start = nullptr;
        for (QWidget* widget : window().topBar()->findChildren<QWidget*>())
        {
            if (takesTab(widget, window()) &&
                (start == nullptr || placeOf(widget).x() < placeOf(start).x() ||
                 (placeOf(widget).x() == placeOf(start).x() && placeOf(widget).y() < placeOf(start).y())))
            {
                start = widget;
            }
        }
        ASSERT_TRUE(takesTab(start, window())) << "no control of the top bar takes the focus from Tab";
        start->setFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        ASSERT_EQ(QApplication::focusWidget(), start);

        std::vector<QWidget*> visited{start};
        QStringList problems;
        constexpr int kMostPresses = 600;
        for (int press = 0; press < kMostPresses; ++press)
        {
            QWidget* const before = QApplication::focusWidget();
            QTest::keyClick(before, Qt::Key_Tab);
            QCoreApplication::processEvents();
            QWidget* const now = QApplication::focusWidget();
            if (now == nullptr || now->window() != &window())
            {
                problems.append(QStringLiteral("Tab from %1 left the window").arg(describe(before)));
                break;
            }
            if (now == start)
            {
                break;
            }
            if (now == before)
            {
                problems.append(QStringLiteral("Tab stays in %1").arg(describe(now)));
                break;
            }
            if (const auto first = std::find(visited.begin(), visited.end(), now); first != visited.end())
            {
                QStringList path;
                for (auto step = first; step != visited.end(); ++step)
                {
                    path.append(describe(*step));
                }
                problems.append(QStringLiteral("%1 is visited twice, after %2")
                                    .arg(describe(now), path.join(QStringLiteral(" > "))));
                break;
            }
            if (goesBack(placeOf(before), placeOf(now)))
            {
                problems.append(QStringLiteral("Tab goes back from %1 (%2,%3) to %4 (%5,%6)")
                                    .arg(describe(before))
                                    .arg(placeOf(before).x())
                                    .arg(placeOf(before).y())
                                    .arg(describe(now))
                                    .arg(placeOf(now).x())
                                    .arg(placeOf(now).y()));
            }
            visited.push_back(now);
        }
        if (QApplication::focusWidget() != start && problems.isEmpty())
        {
            problems.append(QStringLiteral("Tab did not come back to where it began in %1 presses").arg(kMostPresses));
        }
        // Every control that takes the focus from Tab was visited.
        const std::set<QWidget*> seen(visited.begin(), visited.end());
        for (QWidget* widget : window().findChildren<QWidget*>())
        {
            if (takesTab(widget, window()) && seen.count(widget) == 0u)
            {
                problems.append(QStringLiteral("%1 is never visited").arg(describe(widget)));
            }
        }
        EXPECT_TRUE(problems.isEmpty()) << pageName(page) << ", " << visited.size() << " controls visited:\n  "
                                        << problems.join(QStringLiteral("\n  ")).toStdString();
        leave();
    }
}

} // namespace
} // namespace an::tests::observatory
