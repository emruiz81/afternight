// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application's actions: one object each, made from one table, which no window owns.

#include "AfterNightShellCoreExport.h"

#include "app/ActionLookup.h"

#include <QPointer>
#include <QtCore/qnamespace.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

class QAction;
class QObject;

namespace an::ui::shell
{

/// The menu of the menu bar an action belongs to, which is what it acts on: the files, the edits, the view, the image,
/// a process, the extensions, the window's views, help; `None` for an action no menu holds.
enum class ActionGroup : std::uint8_t
{
    File,
    Edit,
    View,
    Image,
    Process,
    Extensions,
    Window,
    Help,
    None,
};

/// What an action of the catalogue is when it is made. Texts are the source texts the application translates under
/// the context `AfterNightAppClass`; a shortcut is written as `QKeySequence` reads it.
struct ActionSpec
{
    std::string_view id;
    const char* text = "";
    ActionGroup group = ActionGroup::None;
    /// The application's icon the action shows, by its name; empty for none.
    const char* iconKey = "";
    const char* shortcut = "";
    Qt::ShortcutContext shortcutContext = Qt::WindowShortcut;
    /// A tool tip of its own; empty, the action's text is its tool tip.
    const char* toolTip = "";
    const char* statusTip = "";
    const char* whatsThis = "";
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
};

/// The application's actions, each made once from `specs()` under the owner the catalogue is given, in the table's
/// order, named by its id: what the menus, the toolbars and the shortcuts of every shell are made of.
///
/// **Hosting.** A shell's window hosts the actions while it lives (`host`): they are its children in Qt's object tree,
/// as a window's own actions are, in the catalogue's order. It gives them back as it goes (`release`). A window that
/// goes while it still hosts them takes them with it, and the catalogue then answers none.
///
/// Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API ActionCatalogue
{
public:
    /// Makes every action under `owner`, which outlives the catalogue or deletes the actions with it.
    explicit ActionCatalogue(QObject& owner);
    ~ActionCatalogue();

    ActionCatalogue(const ActionCatalogue&) = delete;
    ActionCatalogue& operator=(const ActionCatalogue&) = delete;
    ActionCatalogue(ActionCatalogue&&) = delete;
    ActionCatalogue& operator=(ActionCatalogue&&) = delete;

    /// The table the actions are made from, in the catalogue's order.
    [[nodiscard]] static std::span<const ActionSpec> specs() noexcept;

    /// The row of the table under an id; null when the table has none.
    [[nodiscard]] static const ActionSpec* specOf(std::string_view id) noexcept;

    /// The action under an id; null when the catalogue holds none, or no longer does.
    [[nodiscard]] QAction* action(std::string_view id) const;

    /// Every action, in the catalogue's order.
    [[nodiscard]] std::vector<QAction*> all() const;

    /// The actions of a group, in the catalogue's order.
    [[nodiscard]] std::vector<QAction*> inGroup(ActionGroup group) const;

    /// The catalogue as a unit of Controls is handed it; valid while the catalogue lives.
    [[nodiscard]] an::ui::ActionLookup lookup() const;

    /// Makes every action a child of `window`, in the catalogue's order, until `release`.
    void host(QObject& window);

    /// Gives the actions back to the owner.
    void release();

    /// What the actions are children of: a hosting window, or the owner.
    [[nodiscard]] QObject* holder() const;

private:
    QObject& m_owner;
    QPointer<QObject> m_window;
    std::vector<QPointer<QAction>> m_actions;
};

} // namespace an::ui::shell
