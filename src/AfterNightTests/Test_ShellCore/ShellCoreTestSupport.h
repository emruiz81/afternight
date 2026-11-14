// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the shell core's cases share: a settings store held in memory, and descriptors of shells that are never made.

#include "controls/IAppContext.h"
#include "core/IShell.h"
#include "core/ShellRegistry.h"

#include <QHash>
#include <QString>

#include <initializer_list>
#include <memory>
#include <optional>

namespace an::tests::shellcore
{

/// The global settings store, as `an::ui::IGlobalSettingsProvider` answers it, in a hash.
class MemorySettings final : public an::ui::IGlobalSettingsProvider
{
public:
    bool upsert(const QString& key, const QString& value) override
    {
        m_values.insert(key, value);
        return true;
    }

    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        return m_values.value(key, defaultValue);
    }

    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        const auto found = m_values.constFind(key);
        if (found == m_values.cend())
        {
            return std::nullopt;
        }
        return *found;
    }

private:
    QHash<QString, QString> m_values;
};

/// A descriptor under `id` whose shell is never made: the cases choose shells, they do not build them.
[[nodiscard]] inline an::ui::shell::ShellDescriptor descriptorOf(const QString& id, const QString& displayName = {})
{
    an::ui::shell::ShellDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = displayName.isEmpty() ? id : displayName;
    descriptor.create = [](an::ui::shell::ApplicationCore&) { return std::unique_ptr<an::ui::shell::IShell>(); };
    return descriptor;
}

/// A registry holding one descriptor per id, in the order given.
[[nodiscard]] inline an::ui::shell::ShellRegistry registryOf(std::initializer_list<const char*> ids)
{
    an::ui::shell::ShellRegistry registry;
    for (const char* id : ids)
    {
        (void)registry.add(descriptorOf(QString::fromLatin1(id)));
    }
    return registry;
}

} // namespace an::tests::shellcore
