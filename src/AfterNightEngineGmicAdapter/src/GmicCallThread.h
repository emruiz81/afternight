// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - the thread every call into the library runs on.
//
// The library's interpreter recurses once per nested command, up to the 128 levels it allows itself,
// and a filter from its standard command file nests deeply enough that on MSVC one overflows a
// two-megabyte stack. A stack overflow is not a failure anything can report: it ends the process. And
// the stack of the thread that calls this adapter is not something the adapter can choose - a Windows
// thread has what its executable's header states, one megabyte unless the executable was linked to
// say otherwise, and a worker in a consumer's pool has whatever that pool decided.
//
// So the stack a command runs on is this adapter's to provide. Every call is made on a thread started
// for it with `kGmicCallStackBytes` stated, and the calling thread - which would be blocked for the
// call anyway - waits for it, and does the watching a deadline and a progress sink need while it
// waits. `std::thread` cannot state a stack size, which is why the implementation of this header is
// the one unit in the adapter that includes the operating system's thread headers, and why this
// header includes none of them.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace an
{

/// How much stack a call into the library is given, in bytes: eight mebibytes.
///
/// The library's own Windows command-line build asks its linker for six, and the threads its
/// `parallel` command starts ask for eight wherever the library states a size at all. Eight is also
/// the stack a Linux main thread has by default. It is a reservation rather than a commitment: the
/// pages a command never reaches are address space, not memory.
inline constexpr std::size_t kGmicCallStackBytes = std::size_t{8u} * 1024u * 1024u;

/// One call into the library, running on a thread that has the stack the library needs.
///
/// Started by construction and joined by destruction, so nothing outlives the call it was started for.
/// Neither copyable nor movable, because the started thread holds this object's address.
class GmicCallThread
{
public:
    /// Starts `call` on a new thread with `kGmicCallStackBytes` of stack.
    ///
    /// @tparam CallT a callable taking nothing and throwing nothing.
    /// @param call what to run. Borrowed until this object is destroyed, and invoked exactly once when
    ///        `started()` is true and never when it is false.
    template <typename CallT>
    explicit GmicCallThread(CallT& call) noexcept
        : m_invoke([](void* context) noexcept { (*static_cast<CallT*>(context))(); }), m_context(&call)
    {
        start();
    }

    /// Waits for the call to return, however long that takes, and releases the thread.
    ~GmicCallThread();

    GmicCallThread(const GmicCallThread&) = delete;
    GmicCallThread& operator=(const GmicCallThread&) = delete;
    GmicCallThread(GmicCallThread&&) = delete;
    GmicCallThread& operator=(GmicCallThread&&) = delete;

    /// Whether the platform started the thread. When it did not, the call has not run and will not.
    [[nodiscard]] bool started() const noexcept
    {
        return m_started;
    }

    /// Waits until the call has returned or `until` has passed. Only for a thread that started.
    ///
    /// @param until when to stop waiting.
    /// @return true when the call has returned.
    [[nodiscard]] bool waitUntil(std::chrono::steady_clock::time_point until);

    /// Waits until the call has returned. Only for a thread that started.
    void wait();

private:
    /// Asks the platform for the thread.
    void start() noexcept;

    /// What the started thread executes: the call, then the signal that it has returned.
    void runOnThisThread() noexcept;

#if defined(_WIN32)
    /// The entry point `_beginthreadex` is given.
    static unsigned __stdcall windowsEntry(void* self);
#else
    /// The entry point `pthread_create` is given.
    static void* posixEntry(void* self);
#endif

    /// Invokes the borrowed call through its erased type.
    void (*m_invoke)(void*) noexcept;

    /// The borrowed call.
    void* m_context;

    /// Guards the finished flag the calling thread waits on.
    std::mutex m_mutex;

    /// How the started thread tells the calling thread the call has returned.
    std::condition_variable m_finishedSignal;

    /// Whether the call has returned.
    bool m_finished = false;

    /// Whether the platform started the thread.
    bool m_started = false;

    /// The platform's handle for the started thread: a `_beginthreadex` result or a `pthread_t`.
    std::uintptr_t m_native = 0u;
};

} // namespace an
