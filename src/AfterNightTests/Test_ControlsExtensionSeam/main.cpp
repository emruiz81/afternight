// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the extension buffer seam's suite: the shared buffer manager that exports an asset to a planar shared
/// buffer and imports one back, and the host manager's request and result paths over a view's document, compiled
/// against the consumer runtime, the engine and the engine-free protocol library from an explicit list over the
/// viewer's and the process framework's, under a headless QApplication whose event loop delivers what the workers queue
/// to the GUI thread. It launches no extension host: a case that pumps a session as a running host's starts this
/// executable again to stand idle for the host's process (`HostedPanelStandIn`), until its input closes.

#include "Test_ControlsExtensionSeam/HostedPanelStandIn.h"
#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

#include <cstdio>

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsSet(an::tests::seam::kIdleHostVariable))
    {
        while (std::fgetc(stdin) != EOF)
        {
        }
        return 0;
    }
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ControlsExtensionSeam", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
