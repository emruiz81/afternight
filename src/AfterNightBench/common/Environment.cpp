// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "Environment.h"

#include <cstddef>
#include <cstdlib>

namespace an::bench
{

std::string environmentValue(const char* name)
{
    if (name == nullptr)
    {
        return {};
    }

#if defined(_WIN32)
    // MSVC rejects getenv under warnings-as-errors in favour of the bounds-checked form, which
    // hands back an allocation the caller owns.
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

} // namespace an::bench
