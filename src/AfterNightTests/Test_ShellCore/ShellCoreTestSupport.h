// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the shell core's cases share: a settings store held in memory, descriptors of shells that are never made, the
/// processes a case's registry holds, and a storage root of the case's own.

#include "app/ProcessMenuState.h"
#include "app/ProcessRegistry.h"
#include "controls/IAppContext.h"
#include "core/IShell.h"
#include "core/ShellRegistry.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtGlobal>

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

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

/// The processes a case's registry holds, by id, each under the name it is shown by; a case adds and takes them away
/// as extensions come and go. `lookup()` finds them as the process registry's `find` does.
class ProcessCatalogue
{
public:
    ProcessCatalogue(std::initializer_list<std::pair<const char*, const char*>> processes)
    {
        for (const auto& [id, name] : processes)
        {
            add(id, name);
        }
    }

    void add(const std::string& id, const std::string& name)
    {
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = id;
        descriptor.display_name = name;
        m_descriptors[id] = std::move(descriptor);
    }

    void remove(const std::string& id)
    {
        m_descriptors.erase(id);
    }

    [[nodiscard]] an::ui::ProcessDescriptorLookup lookup() const
    {
        return [this](const std::string& id) -> const an::ui::process_registry::ProcessDescriptor* {
            const auto found = m_descriptors.find(id);
            return found == m_descriptors.end() ? nullptr : &found->second;
        };
    }

private:
    std::map<std::string, an::ui::process_registry::ProcessDescriptor> m_descriptors;
};

/// Points `AFTERNIGHT_TEST_STORAGE_ROOT` at a folder of the case's own for its life, and restores what the suite set.
class ScopedStorageRoot
{
public:
    explicit ScopedStorageRoot(const QString& root)
        : m_previous(qgetenv(kVariable)), m_hadPrevious(qEnvironmentVariableIsSet(kVariable))
    {
        qputenv(kVariable, root.toUtf8());
    }

    ~ScopedStorageRoot()
    {
        if (m_hadPrevious)
        {
            qputenv(kVariable, m_previous);
        }
        else
        {
            qunsetenv(kVariable);
        }
    }

    ScopedStorageRoot(const ScopedStorageRoot&) = delete;
    ScopedStorageRoot& operator=(const ScopedStorageRoot&) = delete;

private:
    static constexpr const char* kVariable = "AFTERNIGHT_TEST_STORAGE_ROOT";
    QByteArray m_previous;
    bool m_hadPrevious = false;
};

} // namespace an::tests::shellcore
