// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the AfterNightEngine runtime suite.
//
// The runtime is where the engine stops being a set of value types and starts owning something:
// a context with a lifetime, a concurrency setting, and services that have to be released. The
// cases here are about that ownership rather than about arithmetic, so they are separated from the
// core suite - a failure in this executable says a lifetime rule broke, not that a number is wrong.
//
// Like the core suite, it links AfterNightEngine and gtest and nothing else, and it includes
// only the engine's public headers.

#include <gtest/gtest.h>

// No Qt and no storage-aware service is involved here, so a plain gtest main is all that is
// needed. The other AfterNight test executables own their entry point the same way.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
