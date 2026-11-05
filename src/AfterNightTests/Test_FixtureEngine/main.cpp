// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The engine fixture adapter's suite: neutral descriptions and files into the engine and back, and the engine
// held to the reference expectations checked in under `fixtures/common/expectations/`.

#include "fixtures/harness/QtTestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(
        argc, argv, {"Test_FixtureEngine", an::tests::fixtures::engine::loadedEngineVersion()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
