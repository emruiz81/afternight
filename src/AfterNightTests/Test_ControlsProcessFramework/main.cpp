// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the process framework's suite: the process window, its real-time preview and its runs, the typed
/// parameters, the progress dialog and the channel dispatch, compiled against the consumer runtime and the engine
/// from an explicit list over the viewer they target, under a headless QApplication whose event loop delivers what the
/// runs' workers queue to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ControlsProcessFramework", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
