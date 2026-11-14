// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application's actions as the record of the 1.1 interface holds them, made as plain actions: what a suite below
/// the shell core hands the classic window's builder in place of the shell core's action catalogue, which it cannot
/// link. Each action carries the object name, text, tool tip, shortcut, shortcut context and checkability the record
/// states, so the menus a builder makes from them read as the application's; nothing is bound to them.
#pragma once

#include "fixtures/BaselineRecord.h"

#include <QAction>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <string_view>

namespace an::tests::fixtures
{

/// The record of the application's actions, from the repository's root.
inline constexpr const char* kRecordedActionsPath = "src/AfterNightTests/Test_ShellClassic/data/ActionInventory.txt";

/// The actions the record holds, made under an owner, which deletes them.
class RecordedActions
{
public:
    explicit RecordedActions(QObject& owner) : m_owner(owner)
    {
        const BaselineRecord record(QString::fromLatin1(kRecordedActionsPath), {});
        for (const QString& entry : record.entries(QStringLiteral("actions")))
        {
            const QStringList fields = baselineFields(entry);
            if (fields.size() < 7)
            {
                continue;
            }
            auto* action = new QAction(&m_owner);
            action->setObjectName(fields[0]);
            action->setText(fields[1]);
            action->setToolTip(fields[2]);
            action->setShortcut(QKeySequence::fromString(fields[3], QKeySequence::PortableText));
            action->setShortcutContext(fields[4] == QStringLiteral("application") ? Qt::ApplicationShortcut
                                                                                  : Qt::WindowShortcut);
            action->setCheckable(fields[6] == QStringLiteral("checkable"));
        }
    }

    RecordedActions(const RecordedActions&) = delete;
    RecordedActions& operator=(const RecordedActions&) = delete;

    /// Whether the record was found and held actions.
    [[nodiscard]] bool loaded() const
    {
        return !m_owner.findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly).isEmpty();
    }

    /// The action of an object name; one the record does not hold is made plain under that name, so a builder never
    /// meets a null.
    [[nodiscard]] QAction* action(std::string_view objectName) const
    {
        const QString name = QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size()));
        if (auto* found = m_owner.findChild<QAction*>(name, Qt::FindDirectChildrenOnly))
        {
            return found;
        }
        auto* made = new QAction(&m_owner);
        made->setObjectName(name);
        return made;
    }

    /// The actions by object name, as the catalogue's lookup answers them.
    [[nodiscard]] std::function<QAction*(std::string_view)> lookup() const
    {
        return [this](std::string_view objectName) { return action(objectName); };
    }

private:
    QObject& m_owner;
};

} // namespace an::tests::fixtures
