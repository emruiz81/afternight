// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application's menu bar as the record of the 1.1 interface holds it, made of the recorded actions: what a suite
/// below the shell core reads the application's menus in, since it can link neither the shell core's menu tree nor the
/// classic shell's builder. Each menu carries the object name and title the record states; a menu that follows a
/// person's history is made empty; nothing is bound.
#pragma once

#include "fixtures/BaselineRecord.h"
#include "fixtures/RecordedActions.h"

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QString>
#include <QStringList>

#include <vector>

namespace an::tests::fixtures
{

/// The menu bar the record holds, added to a window's menu bar over recorded actions.
class RecordedMenus
{
public:
    RecordedMenus(QMainWindow& window, const RecordedActions& actions)
    {
        QMenuBar* const bar = window.menuBar();
        const BaselineRecord record(QString::fromLatin1(kRecordedActionsPath), {});
        std::vector<QMenu*> open;
        for (const QString& entry : record.entries(QStringLiteral("menu bar")))
        {
            // Four spaces a level, as the record writes its tree.
            const auto depth = static_cast<std::size_t>(leadingSpaces(entry) / 4);
            const QStringList fields = baselineFields(entry);
            if (fields.isEmpty() || depth > open.size())
            {
                continue;
            }
            open.resize(depth);
            QMenu* const parent = depth == 0 ? nullptr : open.back();
            const QString& kind = fields[0];
            if (kind == QStringLiteral("menu") && fields.size() >= 3)
            {
                auto* menu = new QMenu(fields[2], parent != nullptr ? static_cast<QWidget*>(parent) : bar);
                menu->setObjectName(fields[1]);
                if (parent != nullptr)
                {
                    parent->addMenu(menu);
                }
                else
                {
                    bar->addMenu(menu);
                }
                open.push_back(menu);
            }
            else if (parent == nullptr)
            {
                continue;
            }
            else if (kind == QStringLiteral("action") && fields.size() >= 2)
            {
                parent->addAction(actions.action(fields[1].toStdString()));
            }
            else if (kind == QStringLiteral("separator"))
            {
                parent->addSeparator();
            }
            else if (kind == QStringLiteral("entry") && fields.size() >= 2)
            {
                parent->addAction(fields[1]);
            }
        }
    }

    RecordedMenus(const RecordedMenus&) = delete;
    RecordedMenus& operator=(const RecordedMenus&) = delete;

private:
    [[nodiscard]] static qsizetype leadingSpaces(const QString& entry)
    {
        qsizetype count = 0;
        while (count < entry.size() && entry[count] == QLatin1Char(' '))
        {
            ++count;
        }
        return count;
    }
};

} // namespace an::tests::fixtures
