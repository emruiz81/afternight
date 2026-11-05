// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstdlib>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace TestHelpers
{
class ScopedEnvVar
{
public:
    ScopedEnvVar(const char* name, const char* value) : m_name(name)
    {
#ifdef _WIN32
        char* existing = nullptr;
        size_t len = 0;
        if (_dupenv_s(&existing, &len, name) == 0 && existing)
        {
            m_had_previous = true;
            m_previous_value = existing;
            free(existing);
        }
#else
        if (const char* existing = std::getenv(name))
        {
            m_had_previous = true;
            m_previous_value = existing;
        }
#endif

#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    ~ScopedEnvVar()
    {
#ifdef _WIN32
        if (m_had_previous)
        {
            _putenv_s(m_name.c_str(), m_previous_value.c_str());
        }
        else
        {
            _putenv_s(m_name.c_str(), "");
        }
#else
        if (m_had_previous)
        {
            setenv(m_name.c_str(), m_previous_value.c_str(), 1);
        }
        else
        {
            unsetenv(m_name.c_str());
        }
#endif
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    std::string m_name;
    std::string m_previous_value;
    bool m_had_previous = false;
};
} // namespace TestHelpers
