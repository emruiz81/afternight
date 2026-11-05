// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ThumbnailPersistence.h"

ThumbnailPersistence::ThumbnailPersistence(PersistenceDbExecutor& executor)
    : KeyValueRepository(executor, {"thumbnails", "file_name", "thumbnail_file_name"})
{
}
