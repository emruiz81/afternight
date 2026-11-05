// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "KeyValueRepository.h"

/// Persists per-context folder paths (e.g. last-open directory) in an SQLite
/// key-value table via the shared PersistenceDbExecutor thread.
class AFTERNIGHTCONTROLS_API FolderTrackerPersistence final : public KeyValueRepository
{
public:
    explicit FolderTrackerPersistence(PersistenceDbExecutor& executor);
};
