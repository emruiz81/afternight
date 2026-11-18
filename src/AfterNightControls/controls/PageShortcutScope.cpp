// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/PageShortcutScope.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QShortcut>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace an::ui
{

namespace
{

/// The sequence a key event presses, the keypad's modifier left out: a sequence of Enter matches either Enter key.
[[nodiscard]] QKeySequence pressedSequence(const QKeyEvent& key)
{
    return QKeySequence(QKeyCombination(key.modifiers() & ~Qt::KeypadModifier, static_cast<Qt::Key>(key.key())));
}

/// A held sequence of Enter, Return or another key, the keypad's modifier left out as the pressed one is.
[[nodiscard]] bool matches(const QList<QKeySequence>& keys, const QKeySequence& pressed)
{
    for (const QKeySequence& held : keys)
    {
        if (held.count() != 1)
        {
            continue;
        }
        const QKeyCombination combination = held[0];
        const QKeySequence plain(
            QKeyCombination(combination.keyboardModifiers() & ~Qt::KeypadModifier, combination.key()));
        if (plain == pressed)
        {
            return true;
        }
    }
    return false;
}

/// Whether an action is an entry of a menu.
[[nodiscard]] bool isAMenuEntry(const QAction& action)
{
    for (const QObject* shownIn : action.associatedObjects())
    {
        if (qobject_cast<const QMenu*>(shownIn) != nullptr)
        {
            return true;
        }
    }
    return false;
}

/// Whether an object of the page lies in a window of its own that the page opened - a dialog - and not in the page: a
/// menu is the page's, wherever it pops up.
[[nodiscard]] bool inAWindowOfItsOwn(const QObject& object, const QWidget& page)
{
    for (const QObject* at = object.parent(); at != nullptr && at != &page; at = at->parent())
    {
        const auto* const widget = qobject_cast<const QWidget*>(at);
        if (widget != nullptr && widget->isWindow() && widget->windowType() != Qt::Popup)
        {
            return true;
        }
    }
    return false;
}

/// Whether the focus lies in a widget or one of its children.
[[nodiscard]] bool focusInside(const QWidget* widget)
{
    const QWidget* const focus = QApplication::focusWidget();
    return widget != nullptr && focus != nullptr && (focus == widget || widget->isAncestorOf(focus));
}

} // namespace

PageShortcutScope::PageShortcutScope(QWidget& page) : QObject(&page), m_page(page)
{
    m_page.installEventFilter(this);
}

PageShortcutScope::~PageShortcutScope()
{
    setActive(false);
}

void PageShortcutScope::setActive(const bool active)
{
    if (active == m_active)
    {
        return;
    }
    m_active = active;
    if (active)
    {
        m_actions.clear();
        m_shortcuts.clear();
        for (QAction* action : m_page.findChildren<QAction*>())
        {
            const QList<QKeySequence> keys = action->shortcuts();
            if (keys.isEmpty() || inAWindowOfItsOwn(*action, m_page))
            {
                continue;
            }
            HeldAction held{action, keys, {}, {}};
            action->setShortcuts({});
            // An entry of a menu still says its keys: a menu draws what follows a tab in an entry's text as its keys.
            if (isAMenuEntry(*action) && !action->text().contains(QLatin1Char('\t')))
            {
                held.text = action->text();
                held.textWithKeys = held.text + QLatin1Char('\t') + keys.front().toString(QKeySequence::NativeText);
                action->setText(held.textWithKeys);
            }
            m_actions.push_back(std::move(held));
        }
        for (QShortcut* shortcut : m_page.findChildren<QShortcut*>())
        {
            const QList<QKeySequence> keys = shortcut->keys();
            if (keys.isEmpty() || inAWindowOfItsOwn(*shortcut, m_page))
            {
                continue;
            }
            m_shortcuts.push_back(HeldShortcut{shortcut, keys, shortcut->isEnabled()});
            shortcut->setEnabled(false);
        }
        return;
    }
    for (const HeldAction& held : m_actions)
    {
        if (held.action)
        {
            // Its text is its own again, unless the page gave it another meanwhile.
            if (!held.textWithKeys.isEmpty() && held.action->text() == held.textWithKeys)
            {
                held.action->setText(held.text);
            }
            held.action->setShortcuts(held.keys);
        }
    }
    for (const HeldShortcut& held : m_shortcuts)
    {
        if (held.shortcut)
        {
            held.shortcut->setEnabled(held.wasEnabled);
        }
    }
    m_actions.clear();
    m_shortcuts.clear();
}

bool PageShortcutScope::isActive() const noexcept
{
    return m_active;
}

void PageShortcutScope::addKeys(const QList<QKeySequence>& keys, std::function<bool()> run)
{
    m_own.push_back(OwnKeys{keys, std::move(run)});
}

QList<QKeySequence> PageShortcutScope::heldSequences() const
{
    QList<QKeySequence> sequences;
    const auto add = [&sequences](const QList<QKeySequence>& keys) {
        for (const QKeySequence& key : keys)
        {
            if (!sequences.contains(key))
            {
                sequences.append(key);
            }
        }
    };
    for (const HeldAction& held : m_actions)
    {
        add(held.keys);
    }
    for (const HeldShortcut& held : m_shortcuts)
    {
        add(held.keys);
    }
    for (const OwnKeys& own : m_own)
    {
        add(own.keys);
    }
    return sequences;
}

bool PageShortcutScope::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (!m_active || watched != &m_page || (type != QEvent::ShortcutOverride && type != QEvent::KeyPress))
    {
        return QObject::eventFilter(watched, event);
    }
    auto* const key = static_cast<QKeyEvent*>(event);
    const QKeySequence pressed = pressedSequence(*key);
    if (type == QEvent::KeyPress)
    {
        // The press of a sequence its override ran: nothing runs twice.
        if (!m_ran.isEmpty() && pressed == m_ran)
        {
            m_ran = QKeySequence();
            event->accept();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
    m_ran = QKeySequence();

    // An action of the page's, enabled.
    for (const HeldAction& held : m_actions)
    {
        if (held.action && held.action->isEnabled() && matches(held.keys, pressed))
        {
            m_ran = pressed;
            event->accept();
            held.action->trigger();
            return true;
        }
    }
    // A shortcut of the page's, enabled before the scope took it, in its context.
    for (const HeldShortcut& held : m_shortcuts)
    {
        if (!held.shortcut || !held.wasEnabled || !matches(held.keys, pressed))
        {
            continue;
        }
        const auto* const owner = qobject_cast<const QWidget*>(held.shortcut->parent());
        const Qt::ShortcutContext context = held.shortcut->context();
        const bool inContext =
            owner == nullptr || context == Qt::WindowShortcut || context == Qt::ApplicationShortcut ||
            (context == Qt::WidgetShortcut ? QApplication::focusWidget() == owner : focusInside(owner));
        if (!inContext)
        {
            continue;
        }
        m_ran = pressed;
        event->accept();
        emit held.shortcut->activated();
        return true;
    }
    // The page's own.
    for (const OwnKeys& own : m_own)
    {
        if (!matches(own.keys, pressed))
        {
            continue;
        }
        if (own.run && own.run())
        {
            m_ran = pressed;
            event->accept();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
    return QObject::eventFilter(watched, event);
}

} // namespace an::ui
