// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How the full extension host reaches the engine: the runtime backend module it loads by name, the engine session it
/// borrows from that module, and the order it lets both go in.
///
/// **One session for native and Python work.** The host never opens a session or builds an engine context. The runtime
/// backend's `an_runtime_backend_initialize` opens the process's one session in the backend's module seat; once the
/// interpreter has started, the host asks the embedded `_afternight_runtime` module for its private session capsule,
/// checks it - its name, its record's size, the consumer runtime's interface and the engine's version and generation
/// the backend was built against, and that the session still admits work - and lends that same session to its native
/// context before any native window exists. A capsule that is missing or was built against another tree stops the
/// start.
///
/// **Shutting down is ordered, and checked.** Admission is closed, so no new native work starts; the native windows
/// are destroyed, with their views and documents; the thread pool's work finishes and what it posted back to the GUI
/// thread is delivered, which lets go of the images it held; the host's borrow of the session is let go; the
/// interpreter is finalized, which lets go of the arrays, images and capsules Python held; and the backend is asked to
/// shut its session down. A backend that still sees something holding the session refuses, through its last-error
/// text, and stays loaded, keeping the session until the process ends; only a backend that shut down is unloaded.
#pragma once

#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace an::ui::extensions
{

/// The binary interface version of the runtime backend this host speaks: the backend's five C exports at version 2.
inline constexpr std::uint32_t kRuntimeBackendAbiVersion = 2;

/// Whether a module's reported interface version is the one this host speaks.
///
/// @param reported what the module's `an_runtime_backend_abi_version` answered.
/// @return success for `kRuntimeBackendAbiVersion`; otherwise `ErrorCode::IncompatibleArguments` naming both versions.
[[nodiscard]] an::Status checkRuntimeBackendAbiVersion(std::uint32_t reported);

/// The runtime backend module beside the host: the Debug build's name first in a Debug host, then the Release name.
///
/// @param appDir the directory the host runs from.
/// @return the module's path, or an empty path when none is there.
[[nodiscard]] std::filesystem::path runtimeBackendModulePath(const std::filesystem::path& appDir);

/// The runtime backend module, loaded by file name, and its five C exports.
///
/// The module owns the process's engine session from `initialize` until a `shutdown` it accepts. It is unloaded only
/// once it is not initialized: unloading a module whose session is still held would free the code that session and
/// everything holding it run.
class RuntimeBackendModule
{
public:
    /// A module not loaded yet.
    RuntimeBackendModule() = default;

    /// Shuts the module down, checked, and unloads it when it accepted; a module that refused stays loaded until the
    /// process ends.
    ~RuntimeBackendModule();

    RuntimeBackendModule(const RuntimeBackendModule&) = delete;
    RuntimeBackendModule& operator=(const RuntimeBackendModule&) = delete;
    RuntimeBackendModule(RuntimeBackendModule&&) = delete;
    RuntimeBackendModule& operator=(RuntimeBackendModule&&) = delete;

    /// Loads the module and resolves its exports.
    ///
    /// @param path the module's file.
    /// @return success; `ErrorCode::InvalidState` while a module is loaded and initialized, changing nothing;
    ///         `ErrorCode::ExternalFailure` naming the loader's reason when the file cannot be loaded or lacks an
    ///         export; `ErrorCode::IncompatibleArguments` naming both versions when its interface version is not this
    ///         host's.
    an::Status load(const std::filesystem::path& path);

    /// Whether a module is loaded.
    [[nodiscard]] bool loaded() const noexcept;

    /// Whether the module's `initialize` succeeded and no `shutdown` was accepted since.
    [[nodiscard]] bool initialized() const noexcept;

    /// The interface version the loaded module reports; zero when none is loaded.
    [[nodiscard]] std::uint32_t abiVersion() const noexcept;

    /// Asks the module whether its engine can allocate.
    ///
    /// @return success, or `ErrorCode::ExternalFailure` with the module's last-error text; `ErrorCode::NotInitialized`
    ///         when no module is loaded.
    an::Status probe();

    /// Opens the module's engine session. Idempotent, as the module's own export is.
    ///
    /// @return success, or `ErrorCode::ExternalFailure` with the module's last-error text; `ErrorCode::NotInitialized`
    ///         when no module is loaded.
    an::Status initialize();

    /// Asks the module to shut its session down.
    ///
    /// @return success, also for a module never initialized; `ErrorCode::Busy` with the module's last-error text -
    /// which
    ///         names what still holds the session - when it refused, in which case it stays initialized.
    an::Status shutdown();

    /// Unloads the module.
    ///
    /// @return success, also when nothing is loaded; `ErrorCode::InvalidState` while it is initialized, leaving it
    ///         loaded.
    an::Status unload();

    /// The module's last-error text; empty when no module is loaded or it reports nothing.
    [[nodiscard]] std::string lastErrorText() const;

private:
    void forget() noexcept;

    void* m_handle = nullptr;
    std::uint32_t (*m_abiVersion)() = nullptr;
    int (*m_probe)() = nullptr;
    int (*m_initialize)() = nullptr;
    void (*m_shutdown)() = nullptr;
    const char* (*m_lastError)() = nullptr;
    bool m_initialized = false;
};

/// The engine session the host borrowed from the runtime backend, lent to Controls through the host's context.
///
/// The host holds the session by shared ownership while it is borrowed, so the backend refuses to shut it down until
/// the host lets go.
class HostEngineSession final : public IEngineSessionProvider
{
public:
    /// Nothing borrowed yet.
    HostEngineSession() = default;

    /// Lets the borrow go.
    ~HostEngineSession() override;

    HostEngineSession(const HostEngineSession&) = delete;
    HostEngineSession& operator=(const HostEngineSession&) = delete;
    HostEngineSession(HostEngineSession&&) = delete;
    HostEngineSession& operator=(HostEngineSession&&) = delete;

    /// Borrows the session a capsule holds, after `an::sdk::acceptRuntimeSessionCapsule` checked it.
    ///
    /// @param capsuleName the capsule's name, as the interpreter reports it; may be null.
    /// @param capsulePointer the capsule's pointer, as the interpreter reports it; may be null.
    /// @return success; `ErrorCode::InvalidState` when a session is already borrowed, changing nothing; otherwise what
    ///         the check refused, in which case nothing is borrowed.
    an::Status borrow(const char* capsuleName, const void* capsulePointer);

    /// Borrows the session from the running interpreter: imports `_afternight_runtime`, asks it for its session
    /// capsule, borrows what the capsule holds and lets the capsule go. The caller holds the interpreter's lock.
    ///
    /// @return success; `ErrorCode::NotInitialized` when no interpreter runs; `ErrorCode::ExternalFailure` naming
    ///         Python's exception when the module or its capsule cannot be had; otherwise what `borrow` answered.
    an::Status borrowFromInterpreter();

    /// Whether a session is borrowed.
    [[nodiscard]] bool borrowed() const noexcept;

    /// The borrowed session. Calling it while nothing is borrowed is a defect.
    [[nodiscard]] const an::consumer::EngineSession& engineSession() const noexcept override;

    /// How many owners the borrowed session has, the backend's seat and this borrow among them; zero when nothing is
    /// borrowed.
    [[nodiscard]] long owners() const noexcept;

    /// Nothing new is admitted and every request waiting for a thread is refused. Idempotent, and nothing while nothing
    /// is borrowed.
    void closeAdmission() noexcept;

    /// Lets the borrow go. Idempotent.
    void release() noexcept;

private:
    std::shared_ptr<an::consumer::EngineSession> m_session;
};

/// What the host does at the two steps of its teardown that are its own and not the runtime's.
struct HostTeardownSteps
{
    /// Destroys the native windows, with their views and documents.
    std::function<void()> destroyNativeWindows;

    /// Finalizes the interpreter, letting go of what Python held.
    std::function<void()> finalizeInterpreter;
};

/// Tears the host's engine use down in its order, on the GUI thread once its event loop has returned: admission is
/// closed; the native windows are destroyed and the windows closed on the way out deleted; the thread pool's work
/// finishes and what it posted back is delivered; the borrow is let go; the interpreter is finalized; the backend is
/// asked to shut its session down; and the module is unloaded when it accepted.
///
/// @param session the host's borrow.
/// @param backend the runtime backend module.
/// @param steps the host's own steps; either may be empty.
/// @return what the backend's shutdown answered: success, or `ErrorCode::Busy` naming what still holds the session, in
///         which case the module stays loaded.
an::Status shutDownHostRuntime(HostEngineSession& session, RuntimeBackendModule& backend,
                               const HostTeardownSteps& steps);

} // namespace an::ui::extensions
