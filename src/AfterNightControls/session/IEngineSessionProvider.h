// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The service through which Controls reaches the engine session its process already owns.
///
/// Controls never opens a session and never builds an engine context. The application opens one session after
/// logging and before any image work; in the full extension host the runtime backend's `initialize` owns it and the
/// native windows borrow it. Either way exactly one context serves the process, and everything Controls does - a
/// document adopting an image, an execution admitted for a preview, a snapshot taken for the history - runs in it.
/// This interface is how a Controls object is handed that session: through the application context, never through a
/// global and never by opening another.
///
/// The provider lends the session's shared, `const` half: holds, admission, preview reservations and the memory
/// accountant. Changing settings, closing admission and shutting down stay with whoever owns the session.
#pragma once

#include <AfterNightConsumerRuntime/EngineSession.h>

namespace an::ui
{

/// Lends the process's engine session to Controls.
///
/// Implemented by the application context and by the full host's context. The session it lends outlives every
/// Controls object that was handed the provider: the owner closes admission and drains views, history and caches
/// before it shuts the session down.
class IEngineSessionProvider
{
public:
    /// Virtual, so that a provider held by base pointer is destroyed as what it is.
    virtual ~IEngineSessionProvider() = default;

    /// The session every Controls operation runs in.
    ///
    /// Thread-safe: the session's shared half may be used from any thread.
    ///
    /// @return the process's session, valid while the provider is.
    [[nodiscard]] virtual const an::consumer::EngineSession& engineSession() const noexcept = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    IEngineSessionProvider() = default;

    IEngineSessionProvider(const IEngineSessionProvider&) = default;
    IEngineSessionProvider& operator=(const IEngineSessionProvider&) = default;
    IEngineSessionProvider(IEngineSessionProvider&&) = default;
    IEngineSessionProvider& operator=(IEngineSessionProvider&&) = default;
};

} // namespace an::ui
