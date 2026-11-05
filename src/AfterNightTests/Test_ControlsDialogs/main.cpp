// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the dialogs suite: the settings pages, the about and system information dialogs, the dashboard's file
/// cards, the inspector windows and their opener, and the PixelMath editor, compiled against the consumer runtime and
/// the engine from an explicit list over the viewer's, under a headless QApplication whose event loop delivers what
/// the workers queue to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ControlsDialogs", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
