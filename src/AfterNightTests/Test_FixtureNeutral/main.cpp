// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The engine-neutral fixture layer's suite: descriptions, samples, neutral FITS files, statistics and expectation
// records, and the harness that runs every fixture executable. It links no engine.

#include "fixtures/harness/QtTestHarness.h"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_FixtureNeutral", "neutral"});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
