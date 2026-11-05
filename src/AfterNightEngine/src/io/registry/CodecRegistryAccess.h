// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one door to a registry's adapters, kept out of the published surface.
//
// `CodecRegistry::find` is private so that a caller outside the engine cannot reach an adapter
// around the facade's exception, cancellation and representation checks; the facade reaches it
// through this friend.

#pragma once

#include <AfterNightEngine/IO/CodecRegistry.h>

namespace an
{
/// The engine's private access to a registry's adapters; only code behind the facade's checks uses it.
class CodecRegistryAccess
{
public:
    /// The adapter a registry holds for a format.
    /// @param registry the registry to search.
    /// @param format the format to look up.
    /// @return the adapter, valid as long as `registry`; or null when none is installed.
    [[nodiscard]] static const ImageCodec* find(const CodecRegistry& registry, CodecFormat format) noexcept
    {
        return registry.find(format);
    }
};
} // namespace an
