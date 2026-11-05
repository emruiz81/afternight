// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The console's interrupt, turned into a stop request for the run in progress.
///
/// Ctrl+C, Ctrl+Break, closing the console window or a termination request asks the tool to stop. The tool does not
/// die where it stands: every operation a run starts carries the stop token a watch hands out, so the step that is
/// running stops at its next checkpoint, the output transaction removes what it had begun to write, and the process
/// exits with `ExitCode::Interrupted` having left nothing where the output would have been.
///
/// On Windows the watch is a console control handler; a console that is closing waits, a little less than the five
/// seconds the system allows, for the run to report that it has finished. On Linux it is a handler for SIGINT and
/// SIGTERM that writes one byte to a pipe, which is all a signal handler may safely do, and a thread of the watch's
/// own that reads the pipe and requests the stop.
///
/// The operating system calls a handler with no context, so the watch a handler reaches is held in one process-wide
/// seat, and only one watch is installed at a time: a second while the first lives is not installed.
#pragma once

#include <memory>
#include <stop_token>

namespace an::cli
{

/// Watches the console for an interrupt for as long as it lives.
///
/// Not copyable or movable: the operating system's handler reaches it through the seat while it lives.
class InterruptWatch
{
public:
    /// Installs the handler. A watch that could not be installed - because another is, or because the operating
    /// system refused - says so through `installed` and never stops its token.
    InterruptWatch();

    /// Removes the handler and restores whatever handled the console before.
    ~InterruptWatch();

    InterruptWatch(const InterruptWatch&) = delete;
    InterruptWatch& operator=(const InterruptWatch&) = delete;
    InterruptWatch(InterruptWatch&&) = delete;
    InterruptWatch& operator=(InterruptWatch&&) = delete;

    /// Whether the handler is installed.
    ///
    /// @return true when an interrupt reaches this watch.
    [[nodiscard]] bool installed() const noexcept;

    /// The token an interrupt stops.
    ///
    /// @return the token; it is never stopped when the watch is not installed.
    [[nodiscard]] std::stop_token token() const noexcept;

    /// Whether an interrupt has arrived.
    ///
    /// @return true once the token has been stopped by one.
    [[nodiscard]] bool interrupted() const noexcept;

    /// Says that the run has finished cleaning up, so a console that is closing may let the process go. Idempotent;
    /// the destructor says it too.
    void finished() noexcept;

    /// What the handler reaches: the stop source and the run's progress towards finishing. Defined in the
    /// implementation.
    class State;

private:
    /// The watch's state, shared with the seat while the handler is installed.
    std::shared_ptr<State> m_state;
};

} // namespace an::cli
