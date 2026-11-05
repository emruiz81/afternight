// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The console's interrupt, turned into a stop request for the run in progress.
//
// The two operating systems differ in what their handler may do. A Windows console control handler runs on a thread
// the system starts for the event, so it may take a lock and stop the token itself. A POSIX signal handler interrupts
// whatever thread the signal lands on, and may do almost nothing: it writes one byte to a pipe, and a thread of the
// watch's own reads it and stops the token.

#include "InterruptWatch.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#endif

namespace an::cli
{

/// What the handler reaches: the stop source, whether an interrupt arrived, and whether the run has finished cleaning
/// up, which a closing console waits for.
class InterruptWatch::State
{
public:
    /// Stops the token and records that an interrupt did.
    void requestStop() noexcept
    {
        interruptedFlag.store(true, std::memory_order_release);
        stop.request_stop();
    }

    /// Records that the run has finished and wakes a handler waiting for it.
    void markFinished() noexcept
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_finished = true;
        }
        m_changed.notify_all();
    }

    /// Waits, a bounded time, for the run to finish.
    ///
    /// @param bound how long to wait.
    void waitFinished(std::chrono::milliseconds bound)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_changed.wait_for(lock, bound, [this] { return m_finished; });
    }

    /// What an interrupt stops.
    std::stop_source stop;

    /// Whether an interrupt has arrived.
    std::atomic<bool> interruptedFlag{false};

    /// Whether the handler is installed and reaches this state.
    bool installed = false;

#ifndef _WIN32
    /// The pipe the signal handler writes to: the read end, then the write end.
    int pipeEnds[2] = {-1, -1};

    /// The thread that reads the pipe.
    std::thread watcher;

    /// The handlers the watch replaced, put back when it goes.
    struct sigaction previousInterrupt{};
    struct sigaction previousTermination{};
#endif

private:
    /// Guards `m_finished`.
    std::mutex m_mutex;

    /// Signalled when the run finishes.
    std::condition_variable m_changed;

    /// Whether the run has finished.
    bool m_finished = false;
};

namespace
{

#ifdef _WIN32

/// How long a console that is closing waits for the run to clean up: a little less than the five seconds the system
/// gives a process before it ends it.
constexpr std::chrono::milliseconds kCloseGrace{4500};

/// Where the console control handler, which the system calls with no context, finds the installed watch.
struct InterruptSeat
{
    /// Guards `state`.
    std::mutex mutex;

    /// The installed watch's state, or null.
    std::shared_ptr<InterruptWatch::State> state;
};

/// The one seat. The handler copies the state out under the lock, so a watch going away while the handler runs cannot
/// take the state out from under it.
InterruptSeat interruptSeat;

/// The console control handler.
///
/// @param event what the console reports.
/// @return true when the event was the watch's to handle.
BOOL WINAPI onConsoleControl(DWORD event) noexcept
{
    std::shared_ptr<InterruptWatch::State> state;
    {
        const std::lock_guard<std::mutex> lock(interruptSeat.mutex);
        state = interruptSeat.state;
    }
    if (state == nullptr)
    {
        return FALSE;
    }
    switch (event)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        state->requestStop();
        return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        // The process ends when this returns, so the run is given its cleanup first.
        state->requestStop();
        state->waitFinished(kCloseGrace);
        return TRUE;
    default:
        return FALSE;
    }
}

#else

/// The write end of the installed watch's pipe, or -1 when no watch is installed. A lock-free atomic is the one kind
/// of shared state a signal handler may read, and it doubles as the seat: a watch installs only by moving it from -1.
std::atomic<int> interruptPipe{-1};

static_assert(std::atomic<int>::is_always_lock_free, "a signal handler may read only a lock-free atomic");

/// The byte the handler writes for an interrupt, and the one the watch writes to stop its own thread.
constexpr unsigned char kInterruptByte = 1u;
constexpr unsigned char kStopByte = 0u;

/// The signal handler: one write, and the error number left as it was.
void onInterruptSignal(int) noexcept
{
    const int saved = errno;
    const int descriptor = interruptPipe.load(std::memory_order_acquire);
    if (descriptor >= 0)
    {
        const ssize_t written = ::write(descriptor, &kInterruptByte, 1u);
        static_cast<void>(written);
    }
    errno = saved;
}

/// Reads the pipe until the watch stops it, stopping the token for every interrupt.
void watchPipe(InterruptWatch::State* state) noexcept
{
    for (;;)
    {
        unsigned char byte = kStopByte;
        const ssize_t count = ::read(state->pipeEnds[0], &byte, 1u);
        if (count == 1)
        {
            if (byte == kStopByte)
            {
                return;
            }
            state->requestStop();
            continue;
        }
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        return;
    }
}

/// Closes both ends of a pipe that is open.
void closePipe(int (&ends)[2]) noexcept
{
    for (int& end : ends)
    {
        if (end >= 0)
        {
            ::close(end);
            end = -1;
        }
    }
}

#endif

} // namespace

#ifdef _WIN32

InterruptWatch::InterruptWatch() : m_state(std::make_shared<State>())
{
    const std::lock_guard<std::mutex> lock(interruptSeat.mutex);
    if (interruptSeat.state != nullptr)
    {
        return;
    }
    if (SetConsoleCtrlHandler(onConsoleControl, TRUE) == 0)
    {
        return;
    }
    interruptSeat.state = m_state;
    m_state->installed = true;
}

InterruptWatch::~InterruptWatch()
{
    finished();
    if (!m_state->installed)
    {
        return;
    }
    SetConsoleCtrlHandler(onConsoleControl, FALSE);
    const std::lock_guard<std::mutex> lock(interruptSeat.mutex);
    interruptSeat.state.reset();
}

#else

InterruptWatch::InterruptWatch() : m_state(std::make_shared<State>())
{
    State& state = *m_state;
    if (::pipe2(state.pipeEnds, O_CLOEXEC) != 0)
    {
        state.pipeEnds[0] = -1;
        state.pipeEnds[1] = -1;
        return;
    }
    int vacant = -1;
    if (!interruptPipe.compare_exchange_strong(vacant, state.pipeEnds[1], std::memory_order_acq_rel))
    {
        closePipe(state.pipeEnds);
        return;
    }
    try
    {
        state.watcher = std::thread(watchPipe, &state);
    }
    catch (const std::exception&)
    {
        interruptPipe.store(-1, std::memory_order_release);
        closePipe(state.pipeEnds);
        return;
    }
    struct sigaction handler{};
    handler.sa_handler = onInterruptSignal;
    sigemptyset(&handler.sa_mask);
    // A restarted system call is what the rest of the process expects: only the token says a stop was asked for.
    handler.sa_flags = SA_RESTART;
    const bool interruptInstalled = sigaction(SIGINT, &handler, &state.previousInterrupt) == 0;
    const bool terminationInstalled =
        interruptInstalled && sigaction(SIGTERM, &handler, &state.previousTermination) == 0;
    if (!terminationInstalled)
    {
        if (interruptInstalled)
        {
            sigaction(SIGINT, &state.previousInterrupt, nullptr);
        }
        interruptPipe.store(-1, std::memory_order_release);
        const ssize_t written = ::write(state.pipeEnds[1], &kStopByte, 1u);
        static_cast<void>(written);
        state.watcher.join();
        closePipe(state.pipeEnds);
        return;
    }
    state.installed = true;
}

InterruptWatch::~InterruptWatch()
{
    finished();
    State& state = *m_state;
    if (!state.installed)
    {
        return;
    }
    sigaction(SIGINT, &state.previousInterrupt, nullptr);
    sigaction(SIGTERM, &state.previousTermination, nullptr);
    interruptPipe.store(-1, std::memory_order_release);
    const ssize_t written = ::write(state.pipeEnds[1], &kStopByte, 1u);
    static_cast<void>(written);
    state.watcher.join();
    closePipe(state.pipeEnds);
}

#endif

bool InterruptWatch::installed() const noexcept
{
    return m_state->installed;
}

std::stop_token InterruptWatch::token() const noexcept
{
    return m_state->installed ? m_state->stop.get_token() : std::stop_token{};
}

bool InterruptWatch::interrupted() const noexcept
{
    return m_state->interruptedFlag.load(std::memory_order_acquire);
}

void InterruptWatch::finished() noexcept
{
    m_state->markFinished();
}

} // namespace an::cli
