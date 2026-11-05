// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ModernizationFeatureFlags.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace
{

bool parseFlagValue(std::string_view rawValue, bool defaultValue)
{
    std::string normalized(rawValue);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
        return true;

    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
        return false;

    return defaultValue;
}

} // anonymous namespace

namespace an::ui::modernization
{

bool ModernizationFeatureFlags::readFlagFromEnv(const char* envName, bool defaultValue)
{
    if (!envName)
        return defaultValue;

#ifdef _MSC_VER
    char* raw = nullptr;
    size_t rawLen = 0;
    if (_dupenv_s(&raw, &rawLen, envName) != 0 || !raw || !*raw)
    {
        free(raw);
        return defaultValue;
    }
    std::unique_ptr<char, decltype(&free)> rawGuard(raw, &free);
#else
    const char* raw = std::getenv(envName);
    if (!raw || !*raw)
        return defaultValue;
#endif

    return parseFlagValue(raw, defaultValue);
}

bool ModernizationFeatureFlags::uiTokensEnabled()
{
    return readFlagFromEnv("AN_UI_TOKENS_ENABLED", kUiTokensEnabledDefault);
}

bool ModernizationFeatureFlags::programmaticProcessesEnabled()
{
    return readFlagFromEnv("AN_UI_PROGRAMMATIC_PROCESSES", kProgrammaticProcessesEnabledDefault);
}

bool ModernizationFeatureFlags::dialogModernStyleEnabled()
{
    return readFlagFromEnv("AN_DIALOG_MODERN_STYLE_ENABLED", kDialogModernStyleEnabledDefault);
}

bool ModernizationFeatureFlags::persistenceThreadsafeEnabled()
{
    return readFlagFromEnv("AN_PERSISTENCE_THREADSAFE_ENABLED", kPersistenceThreadsafeEnabledDefault);
}

} // namespace an::ui::modernization
