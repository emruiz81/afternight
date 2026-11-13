// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the application's interface is, read from its own main window as entries of the records beside this suite:
/// the actions the window declares with their text, shortcut, icon and menus; the menu bar's tree; the tree of the
/// menu a view shows on a right click; the key sequences the window and a view answer; and what the process registry
/// states of every process. Each reader walks the live objects in the order a person meets them and reads no
/// geometry, no font and nothing that follows the host, so a record is the same on every platform.
#pragma once

#include "fixtures/BaselineRecord.h"

#include "app/ProcessRegistry.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "widgets/ImageViewWidget.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QKeySequence>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QSet>
#include <QShortcut>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace an::tests::app
{

inline constexpr const char* kActionInventoryPath = "src/AfterNightTests/Test_AfterNightApp/data/ActionInventory.txt";
inline constexpr const char* kShortcutInventoryPath =
    "src/AfterNightTests/Test_AfterNightApp/data/ShortcutInventory.txt";
inline constexpr const char* kViewMenuInventoryPath =
    "src/AfterNightTests/Test_AfterNightApp/data/ViewMenuInventory.txt";
inline constexpr const char* kProcessInventoryPath = "src/AfterNightTests/Test_AfterNightApp/data/ProcessInventory.txt";

inline constexpr const char* kActionsSection = "actions";
inline constexpr const char* kMenuBarSection = "menu bar";
inline constexpr const char* kViewMenuSection = "view menu";
inline constexpr const char* kWindowShortcutsSection = "window";
inline constexpr const char* kConsoleShortcutsSection = "session console";
inline constexpr const char* kProcessesSection = "processes";

/// What every record of the interface closes its preamble with: how it is held and how it changes.
[[nodiscard]] inline QStringList recordKeeping(const QString& heldBy)
{
    return {
        QString(),
        heldBy + QStringLiteral(" in Test_AfterNightApp compares the running application with this record."),
        QStringLiteral("An entry is edited by the change that means to move it - a renamed action, a new menu"),
        QStringLiteral("entry, a retired process; the record is not written again whole. A field is `-` when"),
        QStringLiteral("empty; `\\t` is a tab and `\\|` a bar inside a text."),
    };
}

[[nodiscard]] inline QStringList actionInventoryPreamble()
{
    QStringList preamble = {
        QStringLiteral("The actions and the menu bar of AfterNight 1.1's main window."),
        QString(),
        QStringLiteral("[actions]: every action the window declares, in the order it declares them:"),
        QStringLiteral("  object name | text | tool tip | shortcut | shortcut context | icon | checkable or plain |"),
        QStringLiteral("  the menus of the menu bar that hold it, `;` between two"),
        QStringLiteral("The icon is the name of the application's icon resource the action shows. The actions are"),
        QStringLiteral("read with one image open that was never edited or maximized: Undo, Redo and Maximize View"),
        QStringLiteral("follow the active image."),
        QString(),
        QStringLiteral("[menu bar]: the menu bar's tree, four spaces a level:"),
        QStringLiteral("  menu | object name | title      a menu and, under it, what it holds"),
        QStringLiteral("  action | object name            an action of [actions]"),
        QStringLiteral("  entry | text                    an entry the window adds that [actions] does not hold"),
        QStringLiteral("  separator"),
        QStringLiteral("A menu marked `varies` holds what a person did - the recent files, the favourite and the"),
        QStringLiteral("recent processes - and is held by its name alone; of the Extensions menu, which also lists"),
        QStringLiteral("what is installed, only the actions of [actions] are held. Neither counts among the menus"),
        QStringLiteral("that hold an action."),
    };
    preamble.append(recordKeeping(
        QStringLiteral("ApplicationMenu.ActionInventoryMatchesTheRecord and .MenuBarTreeMatchesTheRecord")));
    return preamble;
}

[[nodiscard]] inline QStringList viewMenuInventoryPreamble()
{
    QStringList preamble = {
        QStringLiteral("The menu a view of AfterNight 1.1 shows on a right click, entry for entry, four spaces a"),
        QStringLiteral("level:"),
        QStringLiteral("  menu | object name | title | the menu bar's    a menu the menu bar holds too: its entries"),
        QStringLiteral("                                                 are those of ActionInventory.txt"),
        QStringLiteral(
            "  menu | object name | title                     a menu of the view's own, its entries under it"),
        QStringLiteral("  action | object name                           an action of ActionInventory.txt"),
        QStringLiteral("  entry | text                                   an entry of the view's own"),
        QStringLiteral("  separator"),
    };
    preamble.append(recordKeeping(QStringLiteral("ApplicationMenu.ViewMenuTreeMatchesTheRecord")));
    return preamble;
}

[[nodiscard]] inline QStringList shortcutInventoryPreamble()
{
    QStringList preamble = {
        QStringLiteral("The key sequences AfterNight 1.1 answers."),
        QString(),
        QStringLiteral("[window]: every shortcut an action or a shortcut object of the main window carries, sorted:"),
        QStringLiteral("  key sequence | context | action or shortcut | its object name, or its text | its owner"),
        QStringLiteral("[session console]: the same of the Session Console, which the window makes when it is first"),
        QStringLiteral("shown and which is read apart, so [window] is the same whether or not it has been."),
        QString(),
        QStringLiteral("[view standard], [view rt_preview], [view st_preview]: the key presses a view's own key"),
        QStringLiteral("handling takes for itself in each mode of a view - an image's view, a process's real-time"),
        QStringLiteral("preview, a list's preview - of every letter, digit and function key, Space and the editing"),
        QStringLiteral("and navigation keys, alone and with Shift, Ctrl and Ctrl+Shift:"),
        QStringLiteral("  key sequence | what the view says the key does, where one of its controls says so"),
        QStringLiteral("A key is listed when the view takes the press, whether or not it has something to act on."),
        QStringLiteral("Undo and Redo, Ctrl+Z and Ctrl+Y, are the window's."),
    };
    preamble.append(recordKeeping(QStringLiteral("ApplicationWindow.ShortcutsMatchTheRecord")));
    return preamble;
}

[[nodiscard]] inline QStringList processInventoryPreamble()
{
    QStringList preamble = {
        QStringLiteral("The processes AfterNight 1.1 registers, in the registry's order, extensions left out:"),
        QStringLiteral("  id | name | process name | category | launch mode | action | sort order | guards |"),
        QStringLiteral("  outputs | help page"),
        QStringLiteral("The name is the one the menu gives the process. The guards are what a launch asks of the"),
        QStringLiteral("active view; the outputs, what the process states it makes beside its result."),
    };
    preamble.append(recordKeeping(QStringLiteral("ProcessRegistry.InventoryMatchesTheRecord")));
    return preamble;
}

/// A menu's title or an entry's text without its mnemonic marks: `&File` is File, `Save && Close` is Save & Close.
[[nodiscard]] inline QString withoutMnemonic(const QString& text)
{
    QString plain;
    plain.reserve(text.size());
    for (qsizetype index = 0; index < text.size(); ++index)
    {
        if (text[index] == QLatin1Char('&') && index + 1 < text.size())
        {
            ++index;
        }
        plain += text[index];
    }
    return plain;
}

[[nodiscard]] inline QString shortcutContextName(Qt::ShortcutContext context)
{
    switch (context)
    {
    case Qt::WidgetShortcut:
        return QStringLiteral("widget");
    case Qt::WidgetWithChildrenShortcut:
        return QStringLiteral("widget and children");
    case Qt::WindowShortcut:
        return QStringLiteral("window");
    case Qt::ApplicationShortcut:
        return QStringLiteral("application");
    }
    return QStringLiteral("unknown");
}

/// The names of the application's icons: an icon an action shows is named by the resource it was loaded from.
class IconNames
{
public:
    explicit IconNames(an::ui::IIconProvider& provider)
    {
        for (const char* folder : {":/AfterNight/icons/SVG", ":/AfterNight/icons/PNG"})
        {
            const QFileInfoList resources = QDir(QString::fromLatin1(folder)).entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo& resource : resources)
            {
                const QIcon icon = provider.getIcon(resource.baseName());
                if (!icon.isNull())
                {
                    m_names.insert(icon.cacheKey(), resource.baseName());
                }
            }
        }
    }

    /// How many icons the application loaded.
    [[nodiscard]] qsizetype size() const
    {
        return m_names.size();
    }

    /// The name of an icon; empty for no icon, `unnamed` for one that is none of the application's.
    [[nodiscard]] QString of(const QIcon& icon) const
    {
        if (icon.isNull())
        {
            return {};
        }
        return m_names.value(icon.cacheKey(), QStringLiteral("unnamed"));
    }

private:
    QHash<qint64, QString> m_names;
};

/// The actions a window declares: its own children that carry an object name, in the order it made them.
[[nodiscard]] inline QList<QAction*> declaredActions(const QWidget& window)
{
    QList<QAction*> declared;
    for (QAction* action : window.findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (!action->objectName().isEmpty())
        {
            declared.append(action);
        }
    }
    return declared;
}

/// The menus whose entries follow what a person did: the recent files, the favourite processes and the recent
/// processes. A record holds them by name alone.
[[nodiscard]] inline bool followsHistory(const QMenu& menu)
{
    static constexpr std::array<const char*, 3u> kHistoryMenus{"menuOpen_Recent", "menuFavorite_Processes",
                                                               "menuRecent_Processes"};
    return std::any_of(kHistoryMenus.begin(), kHistoryMenus.end(),
                       [&menu](const char* name) { return menu.objectName() == QLatin1String(name); });
}

/// The menu that lists the installed extensions beside its own actions: a record holds its own actions alone.
[[nodiscard]] inline bool listsWhatIsInstalled(const QMenu& menu)
{
    return menu.objectName() == QLatin1String("menuExtensions");
}

/// Every menu a menu bar reaches through its submenus.
inline void collectMenus(const QList<QAction*>& entries, QSet<const QMenu*>& menus)
{
    for (const QAction* entry : entries)
    {
        const QMenu* const menu = entry != nullptr ? entry->menu() : nullptr;
        if (menu != nullptr && !menus.contains(menu))
        {
            menus.insert(menu);
            collectMenus(menu->actions(), menus);
        }
    }
}

/// The menus of a menu bar that hold each action, by the titles a person reads down to it: `Image > Mask`.
inline void collectMenuPaths(const QList<QAction*>& entries, const QString& path,
                             QHash<const QAction*, QStringList>& paths)
{
    for (const QAction* entry : entries)
    {
        if (entry == nullptr || entry->isSeparator())
        {
            continue;
        }
        if (const QMenu* const menu = entry->menu())
        {
            if (followsHistory(*menu))
            {
                continue;
            }
            const QString title = withoutMnemonic(menu->title());
            collectMenuPaths(menu->actions(), path.isEmpty() ? title : path + QStringLiteral(" > ") + title, paths);
        }
        else if (!path.isEmpty() && !paths[entry].contains(path))
        {
            paths[entry].append(path);
        }
    }
}

/// [actions]: every action the window declares, an entry each.
[[nodiscard]] inline QStringList actionInventoryEntries(QMainWindow& window, const IconNames& icons)
{
    QHash<const QAction*, QStringList> paths;
    collectMenuPaths(window.menuBar()->actions(), QString(), paths);
    QStringList entries;
    for (const QAction* action : declaredActions(window))
    {
        entries.append(an::tests::fixtures::baselineEntry({
            action->objectName(),
            action->text(),
            action->toolTip(),
            action->shortcut().toString(QKeySequence::PortableText),
            shortcutContextName(action->shortcutContext()),
            icons.of(action->icon()),
            action->isCheckable() ? QStringLiteral("checkable") : QStringLiteral("plain"),
            paths.value(action).join(QStringLiteral("; ")),
        }));
    }
    return entries;
}

/// The entries of a menu as a tree under `depth`. A submenu `shared` holds - a menu the menu bar holds too, when the
/// tree is another menu's - is named and not walked, and so is one that follows a person's history; `declared` are
/// the actions [actions] holds, named by their object name, every other entry by its text. With `declaredOnly`, for
/// the menu that lists what is installed, only the declared actions are entries.
inline void appendMenuTree(const QList<QAction*>& menuEntries, int depth, const QSet<const QMenu*>& shared,
                           const QSet<const QAction*>& declared, QStringList& entries, bool declaredOnly = false)
{
    using an::tests::fixtures::baselineEntry;
    for (const QAction* entry : menuEntries)
    {
        if (entry == nullptr || (declaredOnly && !declared.contains(entry)))
        {
            continue;
        }
        QStringList fields;
        const QMenu* const menu = entry->menu();
        if (entry->isSeparator())
        {
            fields = {QStringLiteral("separator")};
        }
        else if (menu != nullptr)
        {
            fields = {QStringLiteral("menu"), menu->objectName(), menu->title()};
            if (shared.contains(menu))
            {
                fields.append(QStringLiteral("the menu bar's"));
            }
            else if (followsHistory(*menu))
            {
                fields.append(QStringLiteral("varies"));
            }
        }
        else if (declared.contains(entry))
        {
            fields = {QStringLiteral("action"), entry->objectName()};
        }
        else
        {
            fields = {QStringLiteral("entry"), entry->text()};
        }
        entries.append(baselineEntry(fields, depth));
        if (menu != nullptr && !shared.contains(menu) && !followsHistory(*menu))
        {
            appendMenuTree(menu->actions(), depth + 1, shared, declared, entries, listsWhatIsInstalled(*menu));
        }
    }
}

[[nodiscard]] inline QSet<const QAction*> declaredActionSet(const QWidget& window)
{
    QSet<const QAction*> declared;
    for (const QAction* action : declaredActions(window))
    {
        declared.insert(action);
    }
    return declared;
}

/// [menu bar]: the menu bar's tree.
[[nodiscard]] inline QStringList menuBarTreeEntries(QMainWindow& window)
{
    QStringList entries;
    appendMenuTree(window.menuBar()->actions(), 0, {}, declaredActionSet(window), entries);
    return entries;
}

/// [view menu]: the tree of a menu a view shows, the menus it shares with the window's menu bar named and not walked.
[[nodiscard]] inline QStringList viewMenuTreeEntries(const QMenu& shown, QMainWindow& window)
{
    QSet<const QMenu*> shared;
    collectMenus(window.menuBar()->actions(), shared);
    QStringList entries;
    appendMenuTree(shown.actions(), 0, shared, declaredActionSet(window), entries);
    return entries;
}

/// What owns an action or a shortcut of a part of the interface: the part itself, by the name the record gives it,
/// or its parent's object name, or its parent's class.
[[nodiscard]] inline QString ownerOf(const QObject& object, const QWidget& part, const QString& partName)
{
    const QObject* const parent = object.parent();
    if (parent == nullptr)
    {
        return {};
    }
    if (parent == &part)
    {
        return partName;
    }
    return parent->objectName().isEmpty() ? QString::fromLatin1(parent->metaObject()->className())
                                          : parent->objectName();
}

/// Whether an object is another or sits under it.
[[nodiscard]] inline bool isUnder(const QObject* object, const QObject* ancestor)
{
    for (; object != nullptr; object = object->parent())
    {
        if (object == ancestor)
        {
            return true;
        }
    }
    return false;
}

/// Every key sequence an action or a shortcut object under a part of the interface carries - the main window, the
/// Session Console - sorted, so that the order the part made its own parts in does not move the record. What sits
/// under `apart` is left out: a part read in a section of its own.
[[nodiscard]] inline QStringList shortcutEntries(const QWidget& part, const QString& partName,
                                                 const QObject* apart = nullptr)
{
    using an::tests::fixtures::baselineEntry;
    QStringList entries;
    for (const QAction* action : part.findChildren<QAction*>())
    {
        if (apart != nullptr && isUnder(action, apart))
        {
            continue;
        }
        for (const QKeySequence& sequence : action->shortcuts())
        {
            entries.append(baselineEntry({
                sequence.toString(QKeySequence::PortableText),
                shortcutContextName(action->shortcutContext()),
                QStringLiteral("action"),
                action->objectName().isEmpty() ? action->text() : action->objectName(),
                ownerOf(*action, part, partName),
            }));
        }
    }
    for (const QShortcut* shortcut : part.findChildren<QShortcut*>())
    {
        if (apart != nullptr && isUnder(shortcut, apart))
        {
            continue;
        }
        for (const QKeySequence& sequence : shortcut->keys())
        {
            entries.append(baselineEntry({
                sequence.toString(QKeySequence::PortableText),
                shortcutContextName(shortcut->context()),
                QStringLiteral("shortcut"),
                shortcut->objectName(),
                ownerOf(*shortcut, part, partName),
            }));
        }
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

/// Whether `text` names the key sequence `sequence` as a whole: `Ctrl+P` is not named by `Ctrl+Shift+P` or `F1` by
/// `F11`.
[[nodiscard]] inline bool namesSequence(const QString& text, const QString& sequence)
{
    const auto joins = [](QChar character) { return character.isLetterOrNumber() || character == QLatin1Char('+'); };
    qsizetype from = 0;
    while (true)
    {
        const qsizetype at = text.indexOf(sequence, from);
        if (at < 0)
        {
            return false;
        }
        const qsizetype end = at + sequence.size();
        if ((at == 0 || !joins(text[at - 1])) && (end == text.size() || !joins(text[end])))
        {
            return true;
        }
        from = at + 1;
    }
}

/// The key presses a view's own key handling takes for itself, each with what the view says the key does: the tool
/// tip or the text of the first of its controls that names the sequence. Every letter, digit and function key, Space
/// and the editing and navigation keys is pressed alone and with Shift, Ctrl and Ctrl+Shift, on the widget that shows
/// the image, as the view's filter of that widget sees it - so a press the view takes acts, and the caller closes
/// what it opened.
[[nodiscard]] inline QStringList viewShortcutEntries(ImageViewControl& view)
{
    QObject* const shown = view.getViewWidget();
    if (shown == nullptr)
    {
        return {};
    }
    std::vector<int> keys;
    for (int key = Qt::Key_A; key <= Qt::Key_Z; ++key)
    {
        keys.push_back(key);
    }
    for (int key = Qt::Key_0; key <= Qt::Key_9; ++key)
    {
        keys.push_back(key);
    }
    for (int key = Qt::Key_F1; key <= Qt::Key_F12; ++key)
    {
        keys.push_back(key);
    }
    for (const int key :
         {Qt::Key_Space,  Qt::Key_Escape, Qt::Key_Return,  Qt::Key_Enter, Qt::Key_Tab,   Qt::Key_Backspace,
          Qt::Key_Delete, Qt::Key_Insert, Qt::Key_Plus,    Qt::Key_Minus, Qt::Key_Equal, Qt::Key_Comma,
          Qt::Key_Period, Qt::Key_Left,   Qt::Key_Right,   Qt::Key_Up,    Qt::Key_Down,  Qt::Key_Home,
          Qt::Key_End,    Qt::Key_PageUp, Qt::Key_PageDown})
    {
        keys.push_back(key);
    }
    const std::array<Qt::KeyboardModifiers, 4u> modifiers{
        Qt::KeyboardModifiers(Qt::NoModifier),
        Qt::KeyboardModifiers(Qt::ShiftModifier),
        Qt::KeyboardModifiers(Qt::ControlModifier),
        Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier),
    };
    const QList<QAction*> controls = view.findChildren<QAction*>();

    QStringList entries;
    for (const Qt::KeyboardModifiers held : modifiers)
    {
        for (const int key : keys)
        {
            // The filter is asked as Qt asks it for an event of the watched widget; what it answers is whether the
            // view took the press. A release follows every press the view took, as a person's finger gives one.
            QKeyEvent press(QEvent::KeyPress, key, held);
            if (!static_cast<QObject&>(view).eventFilter(shown, &press))
            {
                continue;
            }
            QKeyEvent release(QEvent::KeyRelease, key, held);
            (void)static_cast<QObject&>(view).eventFilter(shown, &release);

            const QString sequence =
                QKeySequence(QKeyCombination(held, static_cast<Qt::Key>(key))).toString(QKeySequence::PortableText);
            QString said;
            for (const QAction* control : controls)
            {
                if (namesSequence(control->toolTip(), sequence))
                {
                    said = control->toolTip();
                    break;
                }
                if (namesSequence(control->text(), sequence))
                {
                    said = control->text();
                    break;
                }
            }
            entries.append(an::tests::fixtures::baselineEntry({sequence, said}));
        }
    }
    return entries;
}

[[nodiscard]] inline QString launchModeName(an::ui::process_registry::ProcessLaunchMode mode)
{
    switch (mode)
    {
    case an::ui::process_registry::ProcessLaunchMode::SingleImage:
        return QStringLiteral("single image");
    case an::ui::process_registry::ProcessLaunchMode::TopLevel:
        return QStringLiteral("top level");
    case an::ui::process_registry::ProcessLaunchMode::NonInteractive:
        return QStringLiteral("non-interactive");
    }
    return QStringLiteral("unknown");
}

/// [processes]: what the registry states of every process of the application, in the registry's order. An extension's
/// process is left out: which ones are registered follows what is installed.
[[nodiscard]] inline QStringList processInventoryEntries()
{
    QStringList entries;
    for (const an::ui::process_registry::ProcessDescriptor& descriptor :
         an::ui::process_registry::ProcessRegistry::instance().all())
    {
        if (descriptor.is_extension_process)
        {
            continue;
        }
        QStringList guards;
        if (descriptor.requires_exclusive_access)
        {
            guards.append(QStringLiteral("exclusive access"));
        }
        if (descriptor.requires_still_image)
        {
            guards.append(QStringLiteral("still image"));
        }
        if (!descriptor.source_channel_filter.empty())
        {
            QStringList channels;
            for (const int count : descriptor.source_channel_filter)
            {
                channels.append(QString::number(count));
            }
            guards.append(QStringLiteral("channels ") + channels.join(QLatin1Char(' ')));
        }
        QStringList outputs;
        if (descriptor.provides_starless_output)
        {
            outputs.append(QStringLiteral("starless"));
        }
        if (descriptor.provides_star_mask_output)
        {
            outputs.append(QStringLiteral("star mask"));
        }
        entries.append(an::tests::fixtures::baselineEntry({
            QString::fromStdString(descriptor.id),
            QString::fromStdString(descriptor.display_name),
            QString::fromStdString(descriptor.process_name),
            QString::fromStdString(descriptor.category),
            launchModeName(descriptor.launch_mode),
            QString::fromStdString(descriptor.action_object_name),
            QString::number(descriptor.sort_order),
            guards.join(QStringLiteral(", ")),
            outputs.join(QStringLiteral(", ")),
            QString::fromStdString(descriptor.helpPath),
        }));
    }
    return entries;
}

} // namespace an::tests::app
