// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - the capability report, the names of states and reasons, and
// reading a reason back from a failure.
//
// Nothing here includes the library, so the report is the same code in both configurations; the
// process-wide counters it reads live beside the sessions in GmicSession.cpp. A reason is read from
// the failure's typed context field, and a number this build cannot name is read as no reason.

#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>

#include "GmicProcessState.h"

#include <AfterNightEngine/Core/Status.h>

#include <cstdint>

namespace an
{

GmicAdapterCapabilities gmicAdapterCapabilities() noexcept
{
    return currentGmicAdapterCapabilities();
}

const char* gmicAdapterStateName(GmicAdapterState state) noexcept
{
    switch (state)
    {
    case GmicAdapterState::Absent:
        return "Absent";
    case GmicAdapterState::Available:
        return "Available";
    case GmicAdapterState::Loaded:
        return "Loaded";
    case GmicAdapterState::FailedToLoad:
        return "FailedToLoad";
    }
    return "UnknownGmicAdapterState";
}

const char* gmicAdapterReasonName(GmicAdapterReason reason) noexcept
{
    switch (reason)
    {
    case GmicAdapterReason::None:
        return "None";
    case GmicAdapterReason::LibraryNotCompiledIn:
        return "LibraryNotCompiledIn";
    case GmicAdapterReason::LibraryProbeFailed:
        return "LibraryProbeFailed";
    case GmicAdapterReason::OptionsRejected:
        return "OptionsRejected";
    case GmicAdapterReason::AdapterShutDown:
        return "AdapterShutDown";
    case GmicAdapterReason::CallInFlight:
        return "CallInFlight";
    case GmicAdapterReason::CommandEmpty:
        return "CommandEmpty";
    case GmicAdapterReason::CommandFailed:
        return "CommandFailed";
    case GmicAdapterReason::Interrupted:
        return "Interrupted";
    case GmicAdapterReason::BufferBridgeRefused:
        return "BufferBridgeRefused";
    case GmicAdapterReason::SourceNotShareable:
        return "SourceNotShareable";
    case GmicAdapterReason::ResultMissing:
        return "ResultMissing";
    case GmicAdapterReason::ResultImageCountUnexpected:
        return "ResultImageCountUnexpected";
    case GmicAdapterReason::ResultGeometryRejected:
        return "ResultGeometryRejected";
    case GmicAdapterReason::ResultFormatUnexpected:
        return "ResultFormatUnexpected";
    case GmicAdapterReason::ResultNameOverflowed:
        return "ResultNameOverflowed";
    case GmicAdapterReason::CallThreadNotStarted:
        return "CallThreadNotStarted";
    }
    return "UnknownGmicAdapterReason";
}

GmicAdapterReason gmicAdapterReason(const Status& status) noexcept
{
    const StatusDetail* detail = status.detail();
    if (detail == nullptr)
    {
        return GmicAdapterReason::None;
    }
    for (const StatusContextField& field : detail->context)
    {
        if (field.key != ContextKey::AdapterReason || field.kind != ContextValueKind::Count)
        {
            continue;
        }
        const auto reason = static_cast<GmicAdapterReason>(field.count);
        // A number this build cannot name is not a reason it can branch on, so it is reported as no
        // reason rather than as a wrong one - a failure from a newer adapter stays readable.
        return isGmicAdapterReason(reason) ? reason : GmicAdapterReason::None;
    }
    return GmicAdapterReason::None;
}

} // namespace an
