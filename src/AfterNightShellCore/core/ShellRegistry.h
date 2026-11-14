// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The shells the application can make, each known by its descriptor.

#include "AfterNightShellCoreExport.h"
#include "core/IShell.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstddef>
#include <deque>
#include <vector>

namespace an::ui::shell
{

/// The shells one run of the application can make, in the order they were registered.
///
/// The application owns one, as a local object, registers every shell it links before it chooses one, and reads it
/// afterwards. A descriptor the registry hands out stays where it is for the registry's life: registering another
/// moves none.
class AFTERNIGHTSHELLCORE_API ShellRegistry
{
public:
    /// Whether `id` can name a shell: a lowercase ASCII letter, then lowercase letters, digits and hyphens. An id is
    /// typed on a command line and stored in a setting, so it is one spelling, matched as it is written.
    [[nodiscard]] static bool isValidId(QStringView id);

    /// Registers a shell. Refuses, and keeps the registry as it was, a descriptor whose id is not valid or is
    /// registered already, and one that cannot make its shell.
    /// @return whether the descriptor was registered.
    [[nodiscard]] bool add(ShellDescriptor descriptor);

    /// The descriptor registered under `id`, or null.
    [[nodiscard]] const ShellDescriptor* find(QStringView id) const;

    /// Every registered descriptor, in the order they were registered.
    [[nodiscard]] std::vector<const ShellDescriptor*> descriptors() const;

    /// The registered ids, in the order they were registered.
    [[nodiscard]] QStringList ids() const;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool isEmpty() const noexcept;

private:
    // A deque, so that a registration never moves a descriptor handed out before it.
    std::deque<ShellDescriptor> m_descriptors;
};

} // namespace an::ui::shell
