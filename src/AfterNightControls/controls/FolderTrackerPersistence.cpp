// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FolderTrackerPersistence.h"

FolderTrackerPersistence::FolderTrackerPersistence(PersistenceDbExecutor& executor)
    : KeyValueRepository(executor, {"folder_tracker", "context", "folder"})
{
}
