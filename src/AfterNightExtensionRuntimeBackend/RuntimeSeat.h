// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runtime backend's module seat: the one engine session the loaded module owns, and the state its five C exports
/// report through.
///
/// The backend's C interface carries no handle - a host loads the module by name and calls five functions - so
/// whether the backend holds its engine is process state, and this is where it lives. `initialize` opens the session
/// the full host's native windows and its Python extensions share; the embedded Python module reaches it through
/// `seatSession`, and the host borrows it through the session capsule (`RuntimeSessionCapsule.h`). Nothing else
/// reaches the seat: ordinary code is handed a session explicitly.
///
/// Every function here is safe to call from any thread; the C exports are ordered by the seat's own lock. None of them
/// starts or touches a Python interpreter.
#pragma once

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <memory>

namespace an::sdk
{

/// Checks that the backend can run here, as `an_runtime_backend_probe` reports it.
///
/// Before `seatInitialize` it builds a temporary engine context of one thread, creates a one-by-one image in it and
/// releases both; after it, it creates that image in the owned session.
///
/// @return 1 when the backend can run, 0 with the reason in `seatLastError` otherwise.
[[nodiscard]] int seatProbe() noexcept;

/// Opens the backend's engine session, as `an_runtime_backend_initialize` reports it. Idempotent: a seat that holds a
/// session answers success and changes nothing.
///
/// @return 1 when the seat holds a session, 0 with the reason in `seatLastError` otherwise.
[[nodiscard]] int seatInitialize() noexcept;

/// Shuts the backend's session down, as `an_runtime_backend_shutdown` does.
///
/// A shutdown is refused - the seat keeps its session and `seatLastError` says what is still held - while a session
/// capsule or anything taken from the session is outstanding: an image the interpreter holds, an array exported from
/// one, an admitted operation. The host finalizes its interpreter first, which releases all of those, and tries again.
/// A seat that holds no session does nothing and leaves no error.
void seatShutdown() noexcept;

/// The text of the last failure of `seatProbe`, `seatInitialize` or `seatShutdown`, or an empty string after one that
/// succeeded. Valid until the next of those calls.
[[nodiscard]] const char* seatLastError() noexcept;

/// The session the seat holds, shared, or null before `seatInitialize` and after a successful `seatShutdown`.
[[nodiscard]] std::shared_ptr<an::consumer::EngineSession> seatSession() noexcept;

} // namespace an::sdk
