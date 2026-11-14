// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The records of the 1.1 interface the shell core's catalogue, menu tree and bindings are held to - the actions and
/// the menu bar, and the processes the registry holds - and the reading of a built menu in the records' own format.

#include "fixtures/BaselineRecord.h"

#include <QAction>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <vector>

namespace an::tests::shellcore
{

/// The actions and the menu bar of 1.1's main window, beside the suite that drives the window.
inline constexpr const char* kActionRecordPath = "src/AfterNightTests/Test_AfterNightApp/data/ActionInventory.txt";

/// The processes 1.1 registers, beside the same suite.
inline constexpr const char* kProcessRecordPath = "src/AfterNightTests/Test_AfterNightApp/data/ProcessInventory.txt";

/// The entries of a record's section, each as its fields; none when the record or the section is missing.
[[nodiscard]] inline std::vector<QStringList> recordedRows(const char* path, const char* section)
{
    const an::tests::fixtures::BaselineRecord record(QString::fromLatin1(path), {});
    std::vector<QStringList> rows;
    for (const QString& entry : record.entries(QString::fromLatin1(section)))
    {
        rows.push_back(an::tests::fixtures::baselineFields(entry));
    }
    return rows;
}

/// The menus whose entries follow what a person did, which a record holds by name alone.
[[nodiscard]] inline bool followsHistory(const QMenu& menu)
{
    static constexpr std::array<const char*, 3u> kHistoryMenus{"menuOpen_Recent", "menuFavorite_Processes",
                                                               "menuRecent_Processes"};
    for (const char* name : kHistoryMenus)
    {
        if (menu.objectName() == QLatin1String(name))
        {
            return true;
        }
    }
    return false;
}

/// A menu's entries as a tree under `depth`, in the format of the record's [menu bar]: a submenu by its object name and
/// title - `varies` for one that follows a person's history, and not walked - an action of `declared` by its object
/// name, any other entry by its text, and the separators. Of the Extensions menu only the declared actions are held.
inline void appendMenuTree(const QList<QAction*>& menuEntries, int depth, const QSet<const QAction*>& declared,
                           QStringList& entries, bool declaredOnly = false)
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
            if (followsHistory(*menu))
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
        if (menu != nullptr && !followsHistory(*menu))
        {
            appendMenuTree(menu->actions(), depth + 1, declared, entries,
                           menu->objectName() == QLatin1String("menuExtensions"));
        }
    }
}

/// A menu bar as the record's [menu bar] holds it.
[[nodiscard]] inline QStringList menuBarEntries(const QMenuBar& bar, const QList<QAction*>& declared)
{
    QSet<const QAction*> declaredSet;
    for (const QAction* action : declared)
    {
        declaredSet.insert(action);
    }
    QStringList entries;
    appendMenuTree(bar.actions(), 0, declaredSet, entries);
    return entries;
}

} // namespace an::tests::shellcore
