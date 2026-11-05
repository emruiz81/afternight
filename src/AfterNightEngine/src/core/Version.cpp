// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the version and binary-interface generation the library was built from,
// and the comparison a caller makes against its own headers.

#include <AfterNightEngine/Core/Version.h>

namespace an
{

// Each of these reads the same constant the header publishes, which is the whole mechanism: the
// library carries the values from the headers it was compiled with, and a caller compiled against
// a different revision carries different ones. Nothing here can be computed at run time without
// destroying that property.

EngineVersion engineVersion() noexcept
{
    return engineHeaderVersion();
}

const char* engineVersionString() noexcept
{
    return kEngineVersionString;
}

int engineAbiGeneration() noexcept
{
    return kEngineAbiGeneration;
}

EngineCompatibility engineCompatibility(EngineVersion headerVersion, int headerAbiGeneration) noexcept
{
    if (headerAbiGeneration != kEngineAbiGeneration)
    {
        return EngineCompatibility::AbiMismatch;
    }
    if (headerVersion != engineHeaderVersion())
    {
        return EngineCompatibility::VersionMismatch;
    }
    return EngineCompatibility::Compatible;
}

} // namespace an
