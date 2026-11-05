// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "KeyValueRepository.h"

/// Persists source-image to thumbnail-file mappings in an SQLite key-value
/// table via the shared PersistenceDbExecutor thread.
class AFTERNIGHTCONTROLS_API ThumbnailPersistence final : public KeyValueRepository
{
public:
    explicit ThumbnailPersistence(PersistenceDbExecutor& executor);
};
