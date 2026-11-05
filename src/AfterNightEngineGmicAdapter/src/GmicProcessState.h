// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - the process-wide facts about this adapter, without G'MIC.
//
// Two things about this adapter are properties of the process rather than of any object: how many
// sessions are open against the library, and whether the library has answered the probe this adapter
// runs before it trusts it. This header is the vocabulary for both, and it deliberately includes no
// header of the library: the capability report has to be answerable by a caller that has not linked
// one into anything, and a translation unit that assembles it should not acquire the library's
// include closure to do so.
//
// Everything declared here is implemented in GmicSession.cpp, which is the one translation unit that
// includes the library's own header. That is the whole reason the declarations are split out.

#pragma once

#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>

#include <AfterNightEngine/Core/ErrorCode.h>

namespace an
{

/// What this adapter can do in this process right now, assembled from the build facts and the
/// counters.
///
/// Thread-safe and allocation-free: the session count, the call count, the probe flag and the
/// last-failure record are atomics, so this takes no lock and can be called from anywhere, including
/// while another thread is opening a session.
///
/// @return the capability report `gmicAdapterCapabilities()` returns.
[[nodiscard]] GmicAdapterCapabilities currentGmicAdapterCapabilities() noexcept;

/// Records why the last attempt to open a session in this process failed after the library was
/// asked to answer.
///
/// Kept so the capability report can say `FailedToLoad` with a reason afterwards, which is what tells
/// a deployment problem apart from an adapter nobody has asked for yet. A refusal that happened
/// before the library was asked - a rejected option, a context that cannot lease - is deliberately
/// not recorded here, because nothing was loaded.
///
/// Thread-safe and allocation-free.
///
/// @param code the failure's code.
/// @param reason which of this adapter's rules refused.
void recordGmicStartupFailure(ErrorCode code, GmicAdapterReason reason) noexcept;

} // namespace an
