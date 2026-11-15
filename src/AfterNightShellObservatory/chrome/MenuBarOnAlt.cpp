// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/MenuBarOnAlt.h"

#include "actions/MenuTree.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

namespace an::ui::observatory
{

namespace
{

/// The letter a menu's title opens it by: the one after its `&`, or its first.
[[nodiscard]] QChar mnemonicOf(const QString& title)
{
    const qsizetype marked = title.indexOf(QLatin1Char('&'));
    if (marked >= 0 && marked + 1 < title.size() && title.at(marked + 1) != QLatin1Char('&'))
    {
        return title.at(marked + 1).toUpper();
    }
    for (const QChar character : title)
    {
        if (character.isLetterOrNumber())
        {
            return character.toUpper();
        }
    }
    return {};
}

/// The letter a key press names, A to Z or 0 to 9; none for another key.
[[nodiscard]] QChar letterOf(const int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
    {
        return QChar(QLatin1Char(static_cast<char>('A' + (key - Qt::Key_A))));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
    {
        return QChar(QLatin1Char(static_cast<char>('0' + (key - Qt::Key_0))));
    }
    return {};
}

} // namespace

MenuBarOnAlt::MenuBarOnAlt(QMainWindow& window, const an::ui::ActionLookup& actions, const bool alwaysShown)
    : QObject(&window), m_window(window), m_alwaysShown(alwaysShown)
{
    auto* bar = new QMenuBar(&window);
    bar->setObjectName(QStringLiteral("observatoryMenuBar"));
    bar->setNativeMenuBar(false);
    an::ui::shell::MenuTree::menuBar().buildMenuBar(*bar, actions);
    window.setMenuBar(bar);
    m_bar = bar;

    for (QAction* top : bar->actions())
    {
        if (QMenu* const menu = top->menu())
        {
            connect(menu, &QMenu::aboutToHide, this, [this]() { concealWhenLeft(); });
        }
    }
    // An entry taken is the end of the visit: the bar goes, and the focus goes back where it was.
    connect(bar, &QMenuBar::triggered, this, [this](QAction* /*action*/) { conceal(); });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget* /*old*/, QWidget* /*now*/) { concealWhenLeft(); });

    bar->setVisible(m_alwaysShown);
    qApp->installEventFilter(this);
}

MenuBarOnAlt::~MenuBarOnAlt()
{
    if (qApp != nullptr)
    {
        qApp->removeEventFilter(this);
    }
}

QMenuBar* MenuBarOnAlt::bar() const noexcept
{
    return m_bar.data();
}

QMenu* MenuBarOnAlt::menu(const std::string_view id) const
{
    if (m_bar.isNull())
    {
        return nullptr;
    }
    return m_bar->findChild<QMenu*>(QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size())));
}

bool MenuBarOnAlt::isAlwaysShown() const noexcept
{
    return m_alwaysShown;
}

void MenuBarOnAlt::setAlwaysShown(const bool alwaysShown)
{
    m_alwaysShown = alwaysShown;
    if (!m_bar.isNull())
    {
        m_bar->setVisible(alwaysShown);
    }
}

bool MenuBarOnAlt::isShown() const
{
    return !m_bar.isNull() && m_bar->isVisible();
}

bool MenuBarOnAlt::isForTheWindow(const QObject* watched) const
{
    const auto* widget = qobject_cast<const QWidget*>(watched);
    return widget != nullptr && widget->window() == &m_window;
}

QMenu* MenuBarOnAlt::menuByLetter(const QChar letter) const
{
    if (m_bar.isNull() || letter.isNull())
    {
        return nullptr;
    }
    for (QAction* top : m_bar->actions())
    {
        QMenu* const menu = top->menu();
        if (menu != nullptr && top->isVisible() && top->isEnabled() && mnemonicOf(top->text()) == letter)
        {
            return menu;
        }
    }
    return nullptr;
}

void MenuBarOnAlt::reveal()
{
    if (m_bar.isNull())
    {
        return;
    }
    QWidget* const focused = QApplication::focusWidget();
    if (focused != m_bar.data())
    {
        m_focusBefore = focused;
    }
    m_bar->show();
    // The bar takes its place in the window before a menu opens under it.
    if (QLayout* const layout = m_window.layout())
    {
        layout->activate();
    }
    m_bar->setFocus(Qt::MenuBarFocusReason);
}

void MenuBarOnAlt::conceal()
{
    if (m_bar.isNull() || m_alwaysShown)
    {
        return;
    }
    const bool hadFocus = m_bar->hasFocus();
    m_bar->hide();
    if (hadFocus && !m_focusBefore.isNull())
    {
        m_focusBefore->setFocus(Qt::MenuBarFocusReason);
    }
    m_focusBefore.clear();
}

void MenuBarOnAlt::concealWhenLeft()
{
    if (m_bar.isNull() || m_alwaysShown || !m_bar->isVisible())
    {
        return;
    }
    // Asked once the event that closed a menu or moved the focus is over: another menu may open from it.
    QTimer::singleShot(0, this, [this]() {
        if (m_bar.isNull() || m_alwaysShown || !m_bar->isVisible() || QApplication::activePopupWidget() != nullptr)
        {
            return;
        }
        if (QApplication::focusWidget() == m_bar.data())
        {
            return;
        }
        m_bar->hide();
        m_focusBefore.clear();
    });
}

void MenuBarOnAlt::openMenu(QMenu* menu)
{
    if (m_bar.isNull() || menu == nullptr)
    {
        return;
    }
    m_bar->setActiveAction(menu->menuAction());
}

bool MenuBarOnAlt::eventFilter(QObject* watched, QEvent* event)
{
    if (event == nullptr || m_bar.isNull())
    {
        return QObject::eventFilter(watched, event);
    }
    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonPress)
    {
        m_altArmed = false;
        return QObject::eventFilter(watched, event);
    }
    if ((type != QEvent::KeyPress && type != QEvent::KeyRelease) || !isForTheWindow(watched))
    {
        return QObject::eventFilter(watched, event);
    }

    const auto* key = static_cast<const QKeyEvent*>(event);
    const Qt::KeyboardModifiers modifiers = key->modifiers() & ~Qt::KeypadModifier;
    if (type == QEvent::KeyPress)
    {
        if (key->key() == Qt::Key_Alt)
        {
            m_altArmed = !key->isAutoRepeat() && (modifiers & ~Qt::AltModifier) == Qt::NoModifier;
            return QObject::eventFilter(watched, event);
        }
        m_altArmed = false;

        // Alt and a menu's letter opens that menu at once.
        if (modifiers == Qt::AltModifier)
        {
            if (QMenu* const menu = menuByLetter(letterOf(key->key())))
            {
                reveal();
                openMenu(menu);
                return true;
            }
            return QObject::eventFilter(watched, event);
        }

        // With the focus on the bar and no menu open, the keys walk into it.
        if (watched == m_bar.data() && QApplication::activePopupWidget() == nullptr && modifiers == Qt::NoModifier)
        {
            switch (key->key())
            {
            case Qt::Key_Down:
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
                for (QAction* top : m_bar->actions())
                {
                    if (top->menu() != nullptr && top->isVisible() && top->isEnabled())
                    {
                        openMenu(top->menu());
                        break;
                    }
                }
                return true;
            case Qt::Key_Escape:
                conceal();
                return true;
            default:
                if (QMenu* const menu = menuByLetter(letterOf(key->key())))
                {
                    openMenu(menu);
                    return true;
                }
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

    // Alt pressed and released alone shows the bar and gives it the focus, or hides it again.
    if (key->key() == Qt::Key_Alt && m_altArmed && !key->isAutoRepeat())
    {
        m_altArmed = false;
        if (m_alwaysShown)
        {
            if (m_bar->hasFocus())
            {
                if (!m_focusBefore.isNull())
                {
                    m_focusBefore->setFocus(Qt::MenuBarFocusReason);
                }
            }
            else
            {
                reveal();
            }
        }
        else if (m_bar->isVisible())
        {
            conceal();
        }
        else
        {
            reveal();
        }
        return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace an::ui::observatory
