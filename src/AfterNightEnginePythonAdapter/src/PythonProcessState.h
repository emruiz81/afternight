// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEnginePythonAdapter - the process-wide facts about this adapter, without Python.
//
// CPython has one interpreter state per process, so "is one running, and did we start it?" is a
// property of the process rather than of any object. This header is the vocabulary for that, and it
// deliberately includes no Python header: the capability report has to be answerable by a caller that
// has not linked a runtime into anything, and a translation unit that assembles it should not acquire
// Python's include closure to do so.
//
// Everything declared here is implemented in PythonRuntime.cpp, which is the one translation unit
// that includes Python's own header. That is the whole reason the declarations are split out.

#pragma once

#include <AfterNightEnginePythonAdapter/PythonAdapterCapabilities.h>

#include <AfterNightEngine/Core/ErrorCode.h>

namespace an
{

/// What this adapter can do in this process right now, assembled from the build facts and the slot.
///
/// Thread-safe and allocation-free: the process slot and the last-failure record are atomics, so this
/// takes no lock and can be called from anywhere, including while another thread is starting an
/// interpreter.
///
/// @return the capability report `pythonAdapterCapabilities()` returns.
[[nodiscard]] PythonAdapterCapabilities currentPythonAdapterCapabilities() noexcept;

/// Records why the last attempt to start an interpreter in this process failed, or clears the record
/// when given `ErrorCode::Ok`, which a successful start does.
///
/// Kept so the capability report can say `FailedToLoad` with a reason afterwards, which is what tells
/// a deployment problem apart from an adapter nobody has asked for yet. A refusal up to and including
/// the engine context's lease - an option, the process slot, a foreign interpreter - is not recorded;
/// the build with no runtime records its refusal, and its report still says `Absent`.
///
/// Thread-safe and allocation-free.
///
/// @param code the failure's code.
/// @param reason which of this adapter's rules refused.
void recordPythonStartupFailure(ErrorCode code, PythonAdapterReason reason) noexcept;

} // namespace an
