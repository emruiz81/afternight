// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The private handshake through which the full extension host borrows the runtime backend's engine session.
///
/// One process runs native process windows and Python extensions side by side, and both reach images through one
/// engine session: the one the runtime backend opened in `an_runtime_backend_initialize`. The backend's C exports
/// carry no handle, so the session crosses to the host the only way the host and the backend both reach after the
/// interpreter starts - the embedded `_afternight_runtime` module's private `_session_capsule()`, which answers a
/// Python capsule holding this record. The host reads the record with `acceptRuntimeSessionCapsule`, which checks it
/// before anything uses the session, and hands that same session to its native context before any native window
/// exists. A capsule is never serialized and never leaves its process.
///
/// The record holds the session by shared ownership, so a capsule the interpreter still holds keeps the backend from
/// shutting its session down: the host finalizes Python before it shuts the backend down, and the backend refuses a
/// shutdown, through its last-error text, while a capsule or anything else of the session is still held.
///
/// This header names no Python type, so the host includes it beside its own interpreter headers, and a check of the
/// record runs without an interpreter.
#pragma once

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Core/Version.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace an::sdk
{

/// The name the session capsule carries; a capsule under any other name is refused.
inline constexpr const char* kRuntimeSessionCapsuleName = "afternight._afternight_runtime.session";

/// What the session capsule holds: the session, and the interface versions the backend was built against.
///
/// Built by the backend and read by the host, both in-tree and built by one toolchain; `recordBytes` and the versions
/// are what catch a host and a backend built from different trees.
struct RuntimeSessionCapsule
{
    /// The size of the record the backend built.
    std::uint32_t recordBytes = sizeof(RuntimeSessionCapsule);

    /// The consumer runtime's interface version the backend was compiled against.
    std::uint32_t consumerRuntimeInterfaceVersion = an::consumer::kConsumerRuntimeInterfaceVersion;

    /// The engine's interface generation the backend was compiled against.
    int engineAbiGeneration = an::kEngineAbiGeneration;

    /// The engine version the backend was compiled against.
    an::EngineVersion engineVersion = an::engineHeaderVersion();

    /// The session, shared with the backend's module seat.
    std::shared_ptr<an::consumer::EngineSession> session{};
};

/// Reads a session capsule the host received from the embedded module, refusing one it must not use.
///
/// @param name the capsule's name, as the interpreter reports it; may be null.
/// @param pointer the capsule's pointer, as the interpreter reports it; may be null.
/// @return the session; `ErrorCode::IncompatibleArguments` naming what differs when the capsule is not the backend's
///         session record, or was built against another consumer runtime or engine than the host and the libraries it
///         loaded; `ErrorCode::NotInitialized` when it holds no session; `ErrorCode::InvalidState` when the session no
///         longer admits work.
[[nodiscard]] inline an::Result<std::shared_ptr<an::consumer::EngineSession>>
acceptRuntimeSessionCapsule(const char* name, const void* pointer)
{
    const auto refused = [](an::ErrorCode code, std::string message) {
        return an::Status::failure(code, "the runtime backend's session capsule is refused: " + std::move(message));
    };
    if (name == nullptr || std::strcmp(name, kRuntimeSessionCapsuleName) != 0)
    {
        return refused(an::ErrorCode::IncompatibleArguments, std::string("it is named '") +
                                                                 (name == nullptr ? "" : name) + "', not '" +
                                                                 kRuntimeSessionCapsuleName + "'");
    }
    if (pointer == nullptr)
    {
        return refused(an::ErrorCode::NotInitialized, "it holds no record");
    }
    const auto* record = static_cast<const RuntimeSessionCapsule*>(pointer);
    if (record->recordBytes != sizeof(RuntimeSessionCapsule))
    {
        return refused(an::ErrorCode::IncompatibleArguments, "its record is " + std::to_string(record->recordBytes) +
                                                                 " bytes and this host reads " +
                                                                 std::to_string(sizeof(RuntimeSessionCapsule)));
    }
    if (record->consumerRuntimeInterfaceVersion != an::consumer::kConsumerRuntimeInterfaceVersion ||
        an::consumer::consumerRuntimeInterfaceVersion() != an::consumer::kConsumerRuntimeInterfaceVersion)
    {
        return refused(an::ErrorCode::IncompatibleArguments,
                       "the backend was built against consumer runtime interface " +
                           std::to_string(record->consumerRuntimeInterfaceVersion) + ", this host against " +
                           std::to_string(an::consumer::kConsumerRuntimeInterfaceVersion) +
                           ", and the loaded library is " +
                           std::to_string(an::consumer::consumerRuntimeInterfaceVersion()));
    }
    if (record->engineAbiGeneration != an::kEngineAbiGeneration || record->engineVersion != an::engineHeaderVersion() ||
        !an::engineMatchesHeaders())
    {
        return refused(an::ErrorCode::IncompatibleArguments,
                       "the backend was built against engine interface generation " +
                           std::to_string(record->engineAbiGeneration) + ", this host against " +
                           std::to_string(an::kEngineAbiGeneration) + ", and the loaded engine is " +
                           an::engineVersionString() + " at generation " + std::to_string(an::engineAbiGeneration()));
    }
    if (!record->session || !record->session->valid())
    {
        return refused(an::ErrorCode::NotInitialized, "it holds no session");
    }
    if (record->session->state() != an::consumer::SessionState::Available)
    {
        return refused(an::ErrorCode::InvalidState,
                       std::string("the session is ") + an::consumer::sessionStateName(record->session->state()));
    }
    return record->session;
}

} // namespace an::sdk
