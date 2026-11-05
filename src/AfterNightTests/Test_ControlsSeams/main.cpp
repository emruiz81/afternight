// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the Controls seams suite: the cases that need the Controls classes they exercise compiled into the
/// executable rather than reached in the library - the close-during-work cases, whose freed-memory quarantine replaces
/// the global allocation functions and so sees a class's frees only when the class is compiled here, and the case that
/// observes what the view tells its companions through registered stand-ins. Under a headless QApplication whose event
/// loop delivers what the workers queue to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ControlsSeams", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
