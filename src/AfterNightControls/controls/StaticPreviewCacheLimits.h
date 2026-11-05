// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>

namespace an::ui
{
// How many decoded frames, and how many calibration masters, the static-preview loader keeps.
constexpr size_t kMaxPreviewCacheEntries = 32;
constexpr size_t kMaxPreviewMasterCacheEntries = 8;
} // namespace an::ui
