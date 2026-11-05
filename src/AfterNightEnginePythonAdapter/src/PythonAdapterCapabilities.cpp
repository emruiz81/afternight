// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEnginePythonAdapter - the capability report, the names of states and reasons, and
// reading a reason back from a failure.
//
// Nothing here includes Python, so the report is the same code in both configurations; the process
// slot and the last-failure record it reads live beside the interpreter in PythonRuntime.cpp. A
// reason is read from the failure's typed context field, and a number this build cannot name is read
// as no reason.

#include <AfterNightEnginePythonAdapter/PythonAdapterCapabilities.h>

#include "PythonProcessState.h"

#include <AfterNightEngine/Core/Status.h>

#include <cstdint>

namespace an
{

PythonAdapterCapabilities pythonAdapterCapabilities() noexcept
{
    return currentPythonAdapterCapabilities();
}

const char* pythonAdapterStateName(PythonAdapterState state) noexcept
{
    switch (state)
    {
    case PythonAdapterState::Absent:
        return "Absent";
    case PythonAdapterState::Available:
        return "Available";
    case PythonAdapterState::Loaded:
        return "Loaded";
    case PythonAdapterState::FailedToLoad:
        return "FailedToLoad";
    }
    return "UnknownPythonAdapterState";
}

const char* pythonAdapterReasonName(PythonAdapterReason reason) noexcept
{
    switch (reason)
    {
    case PythonAdapterReason::None:
        return "None";
    case PythonAdapterReason::RuntimeNotCompiledIn:
        return "RuntimeNotCompiledIn";
    case PythonAdapterReason::ForeignInterpreterActive:
        return "ForeignInterpreterActive";
    case PythonAdapterReason::AdapterAlreadyActive:
        return "AdapterAlreadyActive";
    case PythonAdapterReason::InterpreterStartupFailed:
        return "InterpreterStartupFailed";
    case PythonAdapterReason::InterpreterConfigurationRejected:
        return "InterpreterConfigurationRejected";
    case PythonAdapterReason::AdapterShutDown:
        return "AdapterShutDown";
    case PythonAdapterReason::CallInFlight:
        return "CallInFlight";
    case PythonAdapterReason::PythonExceptionRaised:
        return "PythonExceptionRaised";
    case PythonAdapterReason::PythonInterrupted:
        return "PythonInterrupted";
    case PythonAdapterReason::ModuleNotFound:
        return "ModuleNotFound";
    case PythonAdapterReason::AttributeNotFound:
        return "AttributeNotFound";
    case PythonAdapterReason::NotCallable:
        return "NotCallable";
    case PythonAdapterReason::BufferBridgeRefused:
        return "BufferBridgeRefused";
    case PythonAdapterReason::BufferPermissionMismatch:
        return "BufferPermissionMismatch";
    case PythonAdapterReason::UnexpectedResult:
        return "UnexpectedResult";
    }
    return "UnknownPythonAdapterReason";
}

PythonAdapterReason pythonAdapterReason(const Status& status) noexcept
{
    const StatusDetail* detail = status.detail();
    if (detail == nullptr)
    {
        return PythonAdapterReason::None;
    }
    for (const StatusContextField& field : detail->context)
    {
        if (field.key != ContextKey::AdapterReason || field.kind != ContextValueKind::Count)
        {
            continue;
        }
        const auto reason = static_cast<PythonAdapterReason>(field.count);
        // A number this build cannot name is not a reason it can branch on, so it is reported as no
        // reason rather than as a wrong one - a failure from a newer adapter stays readable.
        return isPythonAdapterReason(reason) ? reason : PythonAdapterReason::None;
    }
    return PythonAdapterReason::None;
}

} // namespace an
