// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the shared process helpers' suite: the closed maps from a dialog's text to the engine's names, the
/// combined-image metadata and the sources' linearity, the image-list helpers, the launcher's guards and the registry
/// every process is launched through, run against the processes library, every process opened through
/// the library's own entry points. Runs under a headless QApplication whose event loop delivers what a view's workers
/// queue to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ProcessesShared", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
