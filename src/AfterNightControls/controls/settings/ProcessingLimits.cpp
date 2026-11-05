// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stored processing limits, the session settings they give and the state the Processing page shows.

#include "stdafx.hpp"
#include "controls/settings/ProcessingLimits.h"

#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <algorithm>
#include <limits>

namespace an::ui
{

namespace
{

constexpr std::uint64_t kBytesPerMegabyte = 1024u * 1024u;

/// A stored whole number: zero for a text that is not one or is negative.
[[nodiscard]] std::uint64_t storedCount(const QString& text)
{
    bool ok = false;
    const qlonglong value = text.trimmed().toLongLong(&ok);
    return ok && value > 0 ? static_cast<std::uint64_t>(value) : 0u;
}

/// The engine memory budget a stored limit states.
[[nodiscard]] std::uint64_t budgetOf(const std::uint64_t megabytes) noexcept
{
    if (megabytes == 0u)
    {
        return an::kDefaultMemoryBudgetBytes;
    }
    if (megabytes > (std::numeric_limits<std::uint64_t>::max)() / kBytesPerMegabyte)
    {
        return an::kDefaultMemoryBudgetBytes;
    }
    return megabytes * kBytesPerMegabyte;
}

} // namespace

ProcessingLimits readProcessingLimits(const SettingReader& read)
{
    ProcessingLimits limits;
    if (!read)
    {
        return limits;
    }
    const std::uint64_t threads = storedCount(read(QString::fromLatin1(SettingsKeys::PROCESSING_MAX_THREADS_BATCH),
                                                   QString::number(SettingsDefaults::PROCESSING_MAX_THREADS_BATCH)));
    limits.threads = static_cast<std::uint32_t>((std::min)(threads, std::uint64_t{an::kMaxConfigurableConcurrency}));
    limits.memoryLimitMegabytes = storedCount(read(QString::fromLatin1(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB),
                                                   QString::number(SettingsDefaults::PROCESSING_MEMORY_LIMIT_MB)));
    return limits;
}

void writeProcessingLimits(const ProcessingLimits& limits, const SettingWriter& write)
{
    if (!write)
    {
        return;
    }
    write(QString::fromLatin1(SettingsKeys::PROCESSING_MAX_THREADS_BATCH), QString::number(limits.threads));
    write(QString::fromLatin1(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB), QString::number(limits.memoryLimitMegabytes));
}

std::optional<std::uint32_t> parsePreviewFrameRate(const QString& text)
{
    bool ok = false;
    const qlonglong value = text.trimmed().toLongLong(&ok);
    if (!ok)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::clamp<qlonglong>(value, kMinPreviewFrameRate, kMaxPreviewFrameRate));
}

std::uint32_t readPreviewFrameRate(const SettingReader& read)
{
    constexpr std::uint32_t kDefault = SettingsDefaults::PROCESSING_PREVIEW_FRAME_RATE;
    if (!read)
    {
        return kDefault;
    }
    return parsePreviewFrameRate(
               read(QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE), QString::number(kDefault)))
        .value_or(kDefault);
}

void writePreviewFrameRate(const std::uint32_t rate, const SettingWriter& write)
{
    if (!write)
    {
        return;
    }
    write(QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE),
          QString::number(std::clamp(rate, kMinPreviewFrameRate, kMaxPreviewFrameRate)));
}

an::consumer::EngineSessionSettings sessionSettingsForLimits(const ProcessingLimits& limits,
                                                             an::consumer::EngineSessionSettings base)
{
    base.maxConcurrency = limits.threads == 0u ? an::kUseHostConcurrency : limits.threads;
    base.limits.memoryBudgetBytes = budgetOf(limits.memoryLimitMegabytes);
    return base;
}

ProcessingLimits limitsOfSessionSettings(const an::consumer::EngineSessionSettings& settings) noexcept
{
    ProcessingLimits limits;
    limits.threads = settings.maxConcurrency == an::kUseHostConcurrency ? 0u : settings.maxConcurrency;
    limits.memoryLimitMegabytes = settings.limits.memoryBudgetBytes == an::kDefaultMemoryBudgetBytes
                                      ? 0u
                                      : settings.limits.memoryBudgetBytes / kBytesPerMegabyte;
    return limits;
}

ProcessingLimitsState processingLimitsState(const an::consumer::EngineSession& session, const ProcessingLimits& stated)
{
    ProcessingLimitsState state;
    state.width = session.width();
    const an::consumer::EngineSessionSettings active = session.activeSettings();
    state.memoryBudgetBytes = active.limits.memoryBudgetBytes;
    state.previewThreads = state.width == 0u ? 0u : an::consumer::previewReservationWidth(state.width);

    const an::consumer::EngineSessionSettings next = sessionSettingsForLimits(stated, active);
    state.threadsPendingRestart = next.maxConcurrency != active.maxConcurrency;
    state.memoryPendingRestart = next.limits.memoryBudgetBytes != active.limits.memoryBudgetBytes;
    return state;
}

} // namespace an::ui
