// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the real-time preview window's suite: the window around its preview - its layout, its preview view
/// and preview area, the runs it holds back and refuses, its frame budget and what it hands to the GUI thread - and the
/// dialog layout helpers and builders process windows are laid out with, run against the Controls library, the
/// consumer runtime and the engine, under a headless QApplication whose event loop delivers what the runs' workers
/// queue to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ImageRTPreviewControl", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
