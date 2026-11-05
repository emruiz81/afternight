// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineGmicAdapter - starting, waiting for and joining the thread a call into the
// library runs on.
//
// On Windows the thread comes from `_beginthreadex`, with `kGmicCallStackBytes` stated as a
// reservation rather than a commitment; elsewhere from `pthread_create`, with the size set on its
// attributes. The call marks itself finished under a mutex the calling thread waits on, with or
// without a time limit, and the destructor joins the thread, so a started call always ends before its
// object does - however long the command runs.

#include "GmicCallThread.h"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace an
{

#if !defined(_WIN32)
// The handle is kept in an integer so the header needs no thread header; a platform whose thread
// identity does not fit in one is a platform this adapter has to be told about.
static_assert(sizeof(pthread_t) <= sizeof(std::uintptr_t), "a pthread_t is kept in a std::uintptr_t");
#endif

void GmicCallThread::start() noexcept
{
#if defined(_WIN32)
    // A reservation rather than a commit size, so the pages a command never reaches stay address space.
    m_native = _beginthreadex(nullptr, static_cast<unsigned>(kGmicCallStackBytes), &GmicCallThread::windowsEntry, this,
                              STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    m_started = m_native != 0u;
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
    {
        return;
    }
    pthread_t thread{};
    m_started = pthread_attr_setstacksize(&attributes, kGmicCallStackBytes) == 0 &&
                pthread_create(&thread, &attributes, &GmicCallThread::posixEntry, this) == 0;
    (void)pthread_attr_destroy(&attributes);
    if (m_started)
    {
        std::memcpy(&m_native, &thread, sizeof(thread));
    }
#endif
}

#if defined(_WIN32)
unsigned __stdcall GmicCallThread::windowsEntry(void* self)
{
    static_cast<GmicCallThread*>(self)->runOnThisThread();
    return 0u;
}
#else
void* GmicCallThread::posixEntry(void* self)
{
    static_cast<GmicCallThread*>(self)->runOnThisThread();
    return nullptr;
}
#endif

void GmicCallThread::runOnThisThread() noexcept
{
    m_invoke(m_context);
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = true;
    }
    // Safe to signal after the lock is released: the calling thread may see the flag and return from
    // its wait at once, but it cannot destroy this object before the destructor has joined this thread.
    m_finishedSignal.notify_all();
}

bool GmicCallThread::waitUntil(std::chrono::steady_clock::time_point until)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_finishedSignal.wait_until(lock, until, [this] { return m_finished; });
}

void GmicCallThread::wait()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_finishedSignal.wait(lock, [this] { return m_finished; });
}

GmicCallThread::~GmicCallThread()
{
    if (!m_started)
    {
        return;
    }
#if defined(_WIN32)
    const auto handle = reinterpret_cast<HANDLE>(m_native);
    (void)WaitForSingleObject(handle, INFINITE);
    (void)CloseHandle(handle);
#else
    pthread_t thread{};
    std::memcpy(&thread, &m_native, sizeof(thread));
    (void)pthread_join(thread, nullptr);
#endif
}

} // namespace an
