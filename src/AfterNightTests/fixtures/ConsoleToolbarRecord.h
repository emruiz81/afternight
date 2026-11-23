// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The record of the Session Console's toolbar as AfterNight 1.1 shows it, and how a toolbar is read into its entries:
/// the Observatory shell's console drawer is compared with it, less its own parts.
#pragma once

#include "fixtures/BaselineRecord.h"

#include <QAction>
#include <QComboBox>
#include <QKeySequence>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QWidget>

#include <functional>

namespace an::tests::fixtures
{

inline constexpr const char* kConsoleToolbarRecordPath =
    "src/AfterNightTests/Test_ShellObservatory/data/ConsoleToolbar.txt";
inline constexpr const char* kConsoleToolbarSection = "toolbar";

[[nodiscard]] inline QStringList consoleToolbarPreamble()
{
    return {
        QStringLiteral("The Session Console's toolbar as AfterNight 1.1 shows it, an entry an item, in its order:"),
        QStringLiteral("  action | text | tooltip | shortcut | checkable"),
        QStringLiteral("  separator"),
        QStringLiteral("  label | text | tooltip"),
        QStringLiteral("  combo | its items | the current one | tooltip"),
        QStringLiteral("  widget | its class"),
        QString(),
        QStringLiteral("Written from AfterNight 1.1's console. In Test_ShellObservatory"),
        QStringLiteral(
            "ConsoleDrawer.TheToolbarIsTheConsoles compares the console drawer's with it, less the drawer's"),
        QStringLiteral("own parts."),
        QStringLiteral(
            "An entry is edited by the change that means to move it; the record is not written again whole."),
        QStringLiteral("A field is `-` when empty; `\\t` is a tab and `\\|` a bar inside a text."),
    };
}

/// The entries of a toolbar, in its order, as the record holds them; an action `skip` answers true for is left out.
[[nodiscard]] inline QStringList toolbarEntries(const QToolBar& toolbar,
                                                const std::function<bool(const QAction*)>& skip = {})
{
    QStringList entries;
    for (const QAction* action : toolbar.actions())
    {
        if (action == nullptr || (skip && skip(action)))
        {
            continue;
        }
        if (action->isSeparator())
        {
            entries.append(QStringLiteral("separator"));
            continue;
        }
        // A plain action is shown by the tool button the toolbar made for it; any other widget was added as itself.
        if (QWidget* const widget = toolbar.widgetForAction(const_cast<QAction*>(action));
            widget != nullptr && !widget->inherits("QToolButton"))
        {
            if (const auto* label = qobject_cast<const QLabel*>(widget))
            {
                entries.append(baselineEntry({QStringLiteral("label"), label->text(), label->toolTip()}));
                continue;
            }
            if (const auto* combo = qobject_cast<const QComboBox*>(widget))
            {
                QStringList items;
                for (int index = 0; index < combo->count(); ++index)
                {
                    items.append(combo->itemText(index));
                }
                entries.append(baselineEntry({QStringLiteral("combo"), items.join(QStringLiteral(", ")),
                                              combo->currentText(), combo->toolTip()}));
                continue;
            }
            entries.append(
                baselineEntry({QStringLiteral("widget"), QString::fromLatin1(widget->metaObject()->className())}));
            continue;
        }
        entries.append(baselineEntry({QStringLiteral("action"), action->text(), action->toolTip(),
                                      action->shortcut().toString(QKeySequence::PortableText),
                                      action->isCheckable() ? QStringLiteral("checkable") : QString()}));
    }
    return entries;
}

} // namespace an::tests::fixtures
