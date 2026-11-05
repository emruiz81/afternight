// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the product's fault-injection suite: the engine's fault harness swept over the product's own
/// publication and read paths - the history's snapshots, the integration's published images and documents, the
/// standalone stack, a sequence's publication, a view's read and an extension buffer's import - each over a session
/// built on the sweep's injecting resource, under a headless QApplication.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ConsumerFaults", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
