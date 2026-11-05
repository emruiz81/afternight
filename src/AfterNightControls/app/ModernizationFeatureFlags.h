// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

namespace an::ui::modernization
{

class AFTERNIGHTCONTROLS_API ModernizationFeatureFlags
{
public:
    static constexpr bool kUiTokensEnabledDefault = true;
    static constexpr bool kProgrammaticProcessesEnabledDefault = true;
    static constexpr bool kDialogModernStyleEnabledDefault = true;
    static constexpr bool kPersistenceThreadsafeEnabledDefault = true;

    static bool uiTokensEnabled();
    static bool programmaticProcessesEnabled();
    static bool dialogModernStyleEnabled();
    static bool persistenceThreadsafeEnabled();

private:
    static bool readFlagFromEnv(const char* envName, bool defaultValue);
};

} // namespace an::ui::modernization
