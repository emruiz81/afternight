// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <array>
#include <cstddef>

namespace ImageIntegration::Config
{
constexpr int kAutosaveIntervalMs = 5000;
constexpr int kContextPanelWidthPx = 320;
constexpr int kDebounceIntervalMs = 200;
constexpr int kRunHistoryMaxEntries = 20;
constexpr size_t kRunHistoryMaxTotalBytes = 5ull * 1024ull * 1024ull * 1024ull; // 5 GB

// What a run's report is taken to weigh in its archive - its model and its document together - before either is
// written: a base, an amount for each light and an amount for each master. An archive decides whether a run's
// payloads fit the history before it writes the report, so these bound it from above: each is twice what the
// largest reports the suites write measure.
constexpr long long kReportArchiveBaseBytes = 384ll * 1024ll;
constexpr long long kReportArchiveBytesPerLight = 8ll * 1024ll;
constexpr long long kReportArchiveBytesPerMaster = 64ll * 1024ll;

/// The folders of an output folder that hold the frames a run makes on its way - calibrated, corrected and
/// registered lights - which a later run can make again from the lights.
inline constexpr std::array<const char*, 3> kIntermediateFrameFolders = {"calibrated_lights", "ca_corrected_lights",
                                                                         "registered_lights"};
} // namespace ImageIntegration::Config
