// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A page's own shortcuts: the key sequences of a process's actions and shortcuts, which win inside its page over the
/// main window's.

#include "AfterNightControlsExport.h"

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <vector>

class QAction;
class QEvent;
class QShortcut;
class QWidget;

namespace an::ui
{

/// The shortcuts a process brings, while the process is presented as a page of a window that holds shortcuts of its
/// own. Qt runs neither of two shortcuts that match in one window, so a sequence the page and the window both hold -
/// `Ctrl+O`, `Ctrl+Enter` - would run nothing; and a shortcut narrowed to the page still loses to the window's while
/// the focus is inside the page. Three things together make the page's win:
///
/// - **The objects carry no live shortcut.** While the scope is active, the sequences of the page's actions and
///   `QShortcut`s are taken off them (an action keeps its tooltip; a shortcut is disabled). An action that is an entry
///   of a menu still says its keys there: they are written into its text after a tab, which a menu draws as an entry's
///   keys, and its text is its own again once the scope is inactive.
/// - **The override is accepted.** The page's widget accepts the `QEvent::ShortcutOverride` of a sequence the scope
///   holds, so the window's shortcut is not looked for; a widget inside the page that edits with the key - a line edit
///   with `Delete` - accepts it first and keeps it.
/// - **The override runs it.** The page's widget triggers the action, activates the shortcut or runs the page's own
///   entry as it accepts the override: the press that follows goes to the focused widget, which may take it for itself
///   (a combo box searches its items by the key's text), and reaches the page's widget only when none does, which
///   then lets it go by.
///
/// A `QShortcut` made for a widget and its children runs only with the focus inside that widget, as it does on its own.
/// With the focus outside the page, nothing of this applies and the window's shortcut runs. `Ctrl+Enter` is matched
/// with or without the keypad's modifier.
///
/// An object in a window of its own that the page opened - a dialog's action, a dialog's shortcut - is left as it is:
/// a key pressed in that window never reaches the page, so the scope could take its sequence but not run it.
///
/// Inactive - the process in a window of its own - every sequence is put back and the objects are live again. Used on
/// the GUI thread.
class AFTERNIGHTCONTROLS_API PageShortcutScope final : public QObject
{
public:
    /// A scope over a page's widget, inactive.
    explicit PageShortcutScope(QWidget& page);
    /// Puts every sequence back.
    ~PageShortcutScope() override;

    PageShortcutScope(const PageShortcutScope&) = delete;
    PageShortcutScope& operator=(const PageShortcutScope&) = delete;
    PageShortcutScope(PageShortcutScope&&) = delete;
    PageShortcutScope& operator=(PageShortcutScope&&) = delete;

    /// Takes the sequences of the page's actions and shortcuts off them now - the ones its widget holds when it is made
    /// active - and holds them, or puts them back.
    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept;

    /// A sequence of the page's own that is no object's: `Ctrl+Enter` for the page's primary action. It wins inside the
    /// page while the scope is active, and runs `run`; a run that answers false lets the key go on as if the page held
    /// no such sequence.
    void addKeys(const QList<QKeySequence>& keys, std::function<bool()> run);

    /// The sequences the scope holds now, each once.
    [[nodiscard]] QList<QKeySequence> heldSequences() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct HeldAction
    {
        QPointer<QAction> action;
        QList<QKeySequence> keys;
        /// A menu entry's own text, and its text with its keys after a tab, which it shows while they are held; empty
        /// for an action of no menu.
        QString text;
        QString textWithKeys;
    };
    struct HeldShortcut
    {
        QPointer<QShortcut> shortcut;
        QList<QKeySequence> keys;
        bool wasEnabled = true;
    };
    struct OwnKeys
    {
        QList<QKeySequence> keys;
        std::function<bool()> run;
    };

    QWidget& m_page;
    bool m_active = false;
    std::vector<HeldAction> m_actions;
    std::vector<HeldShortcut> m_shortcuts;
    std::vector<OwnKeys> m_own;

    /// The sequence the last override ran, whose press is let go by when it reaches the page's widget.
    QKeySequence m_ran;
};

} // namespace an::ui
