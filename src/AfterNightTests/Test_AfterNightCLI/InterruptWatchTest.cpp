// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The command-line tool's interrupt watch, in this process: one watch is installed at a time, nothing but an
// interrupt stops its token, and on Linux a signal does and the handler it replaced is put back.
//
// A console interrupt cannot be sent to this process on Windows without reaching every process sharing its console,
// the runner's shell among them, so the Windows half is proved by `CommandLineProcessTest`, which interrupts the
// tool's own binary in a process group of its own.

#include "InterruptWatch.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif

namespace
{

/// Waits, a bounded time, for a token to be stopped.
bool stoppedWithin(const std::stop_token& token, std::chrono::milliseconds bound)
{
    const auto deadline = std::chrono::steady_clock::now() + bound;
    while (!token.stop_requested() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return token.stop_requested();
}

} // namespace

TEST(InterruptWatchTest, OneWatchIsInstalledAtATimeAndTheSeatIsFreedWithIt)
{
    {
        an::cli::InterruptWatch first;
        ASSERT_TRUE(first.installed());
        {
            an::cli::InterruptWatch second;
            EXPECT_FALSE(second.installed());
            EXPECT_FALSE(second.token().stop_requested());
            EXPECT_FALSE(second.interrupted());
        }
        EXPECT_TRUE(first.installed()) << "a second watch going away took the first one's seat";
    }
    an::cli::InterruptWatch again;
    EXPECT_TRUE(again.installed());
}

TEST(InterruptWatchTest, NothingButAnInterruptStopsTheToken)
{
    an::cli::InterruptWatch watch;
    ASSERT_TRUE(watch.installed());
    EXPECT_TRUE(watch.token().stop_possible());
    EXPECT_FALSE(stoppedWithin(watch.token(), std::chrono::milliseconds(50)));
    EXPECT_FALSE(watch.interrupted());
    watch.finished();
    watch.finished();
    EXPECT_FALSE(watch.token().stop_requested());
}

#ifndef _WIN32
namespace
{

std::atomic<int> previousHandlerCalls{0};

void countingHandler(int)
{
    previousHandlerCalls.fetch_add(1);
}

} // namespace

TEST(InterruptWatchTest, ASignalStopsTheTokenAndTheHandlerItReplacedIsPutBack)
{
    for (const int signalNumber : {SIGINT, SIGTERM})
    {
        SCOPED_TRACE(signalNumber);
        struct sigaction counting{};
        counting.sa_handler = countingHandler;
        sigemptyset(&counting.sa_mask);
        struct sigaction original{};
        ASSERT_EQ(sigaction(signalNumber, &counting, &original), 0);
        previousHandlerCalls.store(0);
        {
            an::cli::InterruptWatch watch;
            ASSERT_TRUE(watch.installed());
            ASSERT_EQ(kill(getpid(), signalNumber), 0);
            EXPECT_TRUE(stoppedWithin(watch.token(), std::chrono::seconds(5)));
            EXPECT_TRUE(watch.interrupted());
            EXPECT_EQ(previousHandlerCalls.load(), 0) << "the watch shared the signal with the handler it replaced";
        }
        ASSERT_EQ(kill(getpid(), signalNumber), 0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (previousHandlerCalls.load() == 0 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EXPECT_EQ(previousHandlerCalls.load(), 1) << "the handler the watch replaced was not put back";
        ASSERT_EQ(sigaction(signalNumber, &original, nullptr), 0);
    }
}
#endif
