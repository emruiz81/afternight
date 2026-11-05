// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The full host's half of the runtime backend: the module loaded by name, the session borrowed from its capsule, and
// the order both are let go in.
//
// Python.h must come before any standard or Qt header that pulls in a C library feature header, and an MSVC Debug
// build must not let it see _DEBUG, which would make it ask for the debug interpreter the bundled runtime does not
// ship.
#if defined(_MSC_VER) && defined(_DEBUG)
#define AN_RESTORE_DEBUG_AFTER_PYTHON_H
#undef _DEBUG
#endif

#include <Python.h>

#ifdef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#define _DEBUG
#undef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#endif

#include "HostRuntimeSession.h"

#include "RuntimeSessionCapsule.h"
#include "app/ExtensionCommon.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QThreadPool>

#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace an::ui::extensions
{

namespace
{

#ifdef _WIN32
constexpr const char* kRuntimeBackendFilename = "an_extension_runtime_backend.dll";
constexpr const char* kRuntimeBackendFilenameDebug = "an_extension_runtime_backend_d.dll";
#else
constexpr const char* kRuntimeBackendFilename = "liban_extension_runtime_backend.so";
constexpr const char* kRuntimeBackendFilenameDebug = "liban_extension_runtime_backend_d.so";
#endif

/// The loader's reason for the last failure, in words.
std::string loaderError()
{
#ifdef _WIN32
    const DWORD code = ::GetLastError();
    return "Windows error " + std::to_string(static_cast<unsigned long>(code));
#else
    const char* reason = ::dlerror();
    return reason != nullptr ? std::string(reason) : std::string("unknown loader error");
#endif
}

/// The Python exception now set, in words, cleared.
std::string takePythonError()
{
    PyObject* raised = PyErr_GetRaisedException();
    if (raised == nullptr)
    {
        return "no exception was set";
    }
    std::string text = "an exception was raised";
    if (PyObject* rendered = PyObject_Str(raised); rendered != nullptr)
    {
        if (const char* utf8 = PyUnicode_AsUTF8(rendered); utf8 != nullptr)
        {
            text = utf8;
        }
        Py_DECREF(rendered);
    }
    PyErr_Clear();
    Py_DECREF(raised);
    return text;
}

} // namespace

std::filesystem::path runtimeBackendModulePath(const std::filesystem::path& appDir)
{
#ifdef NDEBUG
    const std::vector<std::filesystem::path> candidates = {appDir / kRuntimeBackendFilename};
#else
    const std::vector<std::filesystem::path> candidates = {appDir / kRuntimeBackendFilenameDebug,
                                                           appDir / kRuntimeBackendFilename};
#endif

    for (const auto& candidate : candidates)
    {
        if (isRegularFile(candidate))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

an::Status checkRuntimeBackendAbiVersion(const std::uint32_t reported)
{
    if (reported == kRuntimeBackendAbiVersion)
    {
        return an::Status::success();
    }
    return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                               "Runtime backend module reported interface version " + std::to_string(reported) +
                                   "; this host speaks version " + std::to_string(kRuntimeBackendAbiVersion));
}

// -- RuntimeBackendModule --

RuntimeBackendModule::~RuntimeBackendModule()
{
    if (const an::Status stopped = shutdown(); stopped.failed())
    {
        // Unloading a module whose session is still held would free the code its holders run; the process keeps it.
        return;
    }
    static_cast<void>(unload());
}

an::Status RuntimeBackendModule::load(const std::filesystem::path& path)
{
    if (m_initialized)
    {
        return an::Status::failure(an::ErrorCode::InvalidState,
                                   "the runtime backend is initialized; it cannot be loaded again");
    }
    if (const an::Status unloaded = unload(); unloaded.failed())
    {
        return unloaded;
    }

#ifdef _WIN32
    HMODULE handle = ::LoadLibraryW(path.wstring().c_str());
#else
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (handle == nullptr)
    {
        return an::Status::failure(an::ErrorCode::ExternalFailure,
                                   "Failed to load the runtime backend module " + path.string() + ": " + loaderError());
    }
    m_handle = handle;

    const auto resolve = [this](const char* symbol) -> void* {
#ifdef _WIN32
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), symbol));
#else
        return ::dlsym(m_handle, symbol);
#endif
    };
    m_abiVersion = reinterpret_cast<std::uint32_t (*)()>(resolve("an_runtime_backend_abi_version"));
    m_probe = reinterpret_cast<int (*)()>(resolve("an_runtime_backend_probe"));
    m_initialize = reinterpret_cast<int (*)()>(resolve("an_runtime_backend_initialize"));
    m_shutdown = reinterpret_cast<void (*)()>(resolve("an_runtime_backend_shutdown"));
    m_lastError = reinterpret_cast<const char* (*)()>(resolve("an_runtime_backend_last_error"));
    if (m_abiVersion == nullptr || m_probe == nullptr || m_initialize == nullptr || m_shutdown == nullptr ||
        m_lastError == nullptr)
    {
        static_cast<void>(unload());
        return an::Status::failure(an::ErrorCode::ExternalFailure,
                                   "Runtime backend module is missing one or more required symbols");
    }

    if (an::Status speaks = checkRuntimeBackendAbiVersion(m_abiVersion()); speaks.failed())
    {
        static_cast<void>(unload());
        return speaks;
    }
    return an::Status::success();
}

bool RuntimeBackendModule::loaded() const noexcept
{
    return m_handle != nullptr;
}

bool RuntimeBackendModule::initialized() const noexcept
{
    return m_initialized;
}

std::uint32_t RuntimeBackendModule::abiVersion() const noexcept
{
    return m_abiVersion != nullptr ? m_abiVersion() : 0u;
}

an::Status RuntimeBackendModule::probe()
{
    if (m_probe == nullptr)
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the runtime backend module is not loaded");
    }
    if (m_probe() == 1)
    {
        return an::Status::success();
    }
    const std::string reason = lastErrorText();
    return an::Status::failure(an::ErrorCode::ExternalFailure,
                               reason.empty() ? std::string("Runtime backend probe failed") : reason);
}

an::Status RuntimeBackendModule::initialize()
{
    if (m_initialize == nullptr)
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the runtime backend module is not loaded");
    }
    if (m_initialize() == 1)
    {
        m_initialized = true;
        return an::Status::success();
    }
    const std::string reason = lastErrorText();
    return an::Status::failure(an::ErrorCode::ExternalFailure,
                               reason.empty() ? std::string("Runtime backend initialization failed") : reason);
}

an::Status RuntimeBackendModule::shutdown()
{
    if (!m_initialized || m_shutdown == nullptr)
    {
        return an::Status::success();
    }
    m_shutdown();
    // The export answers nothing: a refusal is its last-error text, which it clears when it shuts down.
    if (std::string refused = lastErrorText(); !refused.empty())
    {
        LOG_WARNING("The runtime backend kept its engine session: {}", refused);
        return an::Status::failure(an::ErrorCode::Busy, std::move(refused));
    }
    m_initialized = false;
    return an::Status::success();
}

an::Status RuntimeBackendModule::unload()
{
    if (m_initialized)
    {
        return an::Status::failure(an::ErrorCode::InvalidState,
                                   "the runtime backend still owns its engine session; it stays loaded");
    }
    if (m_handle != nullptr)
    {
#ifdef _WIN32
        ::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
        ::dlclose(m_handle);
#endif
    }
    forget();
    return an::Status::success();
}

std::string RuntimeBackendModule::lastErrorText() const
{
    if (m_lastError == nullptr)
    {
        return {};
    }
    const char* value = m_lastError();
    return value != nullptr ? std::string(value) : std::string{};
}

void RuntimeBackendModule::forget() noexcept
{
    m_handle = nullptr;
    m_abiVersion = nullptr;
    m_probe = nullptr;
    m_initialize = nullptr;
    m_shutdown = nullptr;
    m_lastError = nullptr;
}

// -- HostEngineSession --

HostEngineSession::~HostEngineSession()
{
    release();
}

an::Status HostEngineSession::borrow(const char* capsuleName, const void* capsulePointer)
{
    if (m_session)
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "the host has already borrowed the engine session");
    }
    an::Result<std::shared_ptr<an::consumer::EngineSession>> accepted =
        an::sdk::acceptRuntimeSessionCapsule(capsuleName, capsulePointer);
    if (!accepted.hasValue())
    {
        return accepted.status();
    }
    m_session = std::move(*accepted);
    LOG_INFO("Extension host: borrowed the runtime backend's engine session, {} thread(s)", m_session->width());
    return an::Status::success();
}

an::Status HostEngineSession::borrowFromInterpreter()
{
    if (Py_IsInitialized() == 0)
    {
        return an::Status::failure(an::ErrorCode::NotInitialized,
                                   "no interpreter runs to ask for the runtime backend's session");
    }
    PyObject* module = PyImport_ImportModule("_afternight_runtime");
    if (module == nullptr)
    {
        return an::Status::failure(an::ErrorCode::ExternalFailure,
                                   "the runtime module cannot be imported: " + takePythonError());
    }
    PyObject* capsule = PyObject_CallMethod(module, "_session_capsule", nullptr);
    Py_DECREF(module);
    if (capsule == nullptr)
    {
        return an::Status::failure(an::ErrorCode::ExternalFailure,
                                   "the runtime module answered no session capsule: " + takePythonError());
    }

    const char* name = PyCapsule_CheckExact(capsule) != 0 ? PyCapsule_GetName(capsule) : nullptr;
    const void* pointer = name != nullptr ? PyCapsule_GetPointer(capsule, name) : nullptr;
    if (PyErr_Occurred() != nullptr)
    {
        PyErr_Clear();
    }
    // The borrow holds the session by shared ownership of its own, so the capsule is not kept.
    const an::Status borrowed = borrow(name, pointer);
    Py_DECREF(capsule);
    return borrowed;
}

bool HostEngineSession::borrowed() const noexcept
{
    return static_cast<bool>(m_session);
}

const an::consumer::EngineSession& HostEngineSession::engineSession() const noexcept
{
    return *m_session;
}

long HostEngineSession::owners() const noexcept
{
    return m_session ? m_session.use_count() : 0;
}

void HostEngineSession::closeAdmission() noexcept
{
    if (m_session)
    {
        m_session->closeAdmission();
    }
}

void HostEngineSession::release() noexcept
{
    m_session.reset();
}

an::Status shutDownHostRuntime(HostEngineSession& session, RuntimeBackendModule& backend,
                               const HostTeardownSteps& steps)
{
    session.closeAdmission();
    if (steps.destroyNativeWindows)
    {
        steps.destroyNativeWindows();
    }

    // Work still on the pool finishes or is refused, and what it posted back for the GUI thread - a result on its way
    // to a view that is gone, holding the image it was computed from - is delivered and let go before the backend
    // counts what is outstanding.
    //
    // The wait delivers the GUI thread's events as it goes: a worker of the host may be blocked on a call it queued to
    // the GUI thread, and a wait that delivered nothing would never end.
    if (QCoreApplication::instance() != nullptr)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThreadPool* const pool = QThreadPool::globalInstance();
        while (!pool->waitForDone(20))
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        QCoreApplication::sendPostedEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    session.release();
    if (steps.finalizeInterpreter)
    {
        steps.finalizeInterpreter();
    }

    if (const an::Status stopped = backend.shutdown(); stopped.failed())
    {
        return stopped;
    }
    return backend.unload();
}

} // namespace an::ui::extensions
