// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point of the command-line tool's suite.
//
// The suite runs the tool's run in this process and the tool's own binary beside it. Both write files, and the binary
// logs, so the suite runs under the engine-free harness's storage root and temporary directory: the runner's root
// when it gives one, otherwise one this process owns and removes. A child inherits both through the environment, so
// nothing the binary writes reaches the user's own data directory. The suite itself starts no logger and no crash
// handler: the tool's run logs through the platform library only once a process has started one, which the binary
// does and this process does not.

#include "fixtures/harness/TestHarness.h"

#include <gtest/gtest.h>

#include <cstdio>

int main(int argc, char** argv)
{
    an::tests::harness::TestStorage storage("Test_AfterNightCLI");
    if (!storage.valid())
    {
        std::fprintf(stderr, "Test_AfterNightCLI: no storage root could be created\n");
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    if (!storage.release())
    {
        std::fprintf(stderr, "Test_AfterNightCLI: the storage root could not be removed\n");
    }
    return result;
}
