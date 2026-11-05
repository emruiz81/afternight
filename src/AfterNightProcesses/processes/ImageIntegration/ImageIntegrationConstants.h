// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>

namespace ImageIntegration::Config
{
constexpr int kAutosaveIntervalMs = 5000;
constexpr int kContextPanelWidthPx = 320;
constexpr int kDebounceIntervalMs = 200;
constexpr int kRunHistoryMaxEntries = 20;
constexpr size_t kRunHistoryMaxTotalBytes = 5ull * 1024ull * 1024ull * 1024ull; // 5 GB
} // namespace ImageIntegration::Config
