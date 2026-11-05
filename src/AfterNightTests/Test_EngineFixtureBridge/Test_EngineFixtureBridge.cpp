// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the benchmark fixture bridge suite.
//
// This is the one engine suite that links two things: AfterNightEngine and the engine-neutral
// benchmark layer. That is deliberate and it is what the suite is for - the bridge between them is
// the only code in the tree that names a benchmark type and an engine type in one file, and it is
// exactly where a translation can be wrong in a way that produces a measurement of the wrong thing
// rather than a failure.
//
// It links nothing else of AfterNight's, so the cases measure the bridge the benchmark runner uses
// and nothing beside it.

#include <gtest/gtest.h>

// No Qt and no storage-aware service is involved here, so a plain gtest main is all that is
// needed. The other AfterNight test executables own their entry point the same way.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
