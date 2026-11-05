// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "KeyValueRepository.h"

/**
 * @brief A generic key-value persistence store for global application settings.
 *
 * Persists global settings in an SQLite database as string key-value pairs.
 * Values can be serialized/deserialized as needed by the caller.
 *
 * Example settings that can be persisted:
 * - Raw image loading options (white balance mode, brightness settings)
 * - Default export formats
 * - UI preferences
 */
class AFTERNIGHTCONTROLS_API GlobalSettingsPersistence final : public KeyValueRepository
{
public:
    explicit GlobalSettingsPersistence(PersistenceDbExecutor& executor);
};
