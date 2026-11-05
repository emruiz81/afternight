// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GlobalSettingsPersistence.h"

GlobalSettingsPersistence::GlobalSettingsPersistence(PersistenceDbExecutor& executor)
    : KeyValueRepository(executor, {"global_settings", "setting_key", "setting_value"})
{
}
