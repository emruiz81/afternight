// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the mask, star and background processes' suite: mask drawing, range selection, binarize, the star
/// mask, star reduction, dark structure enhancement, the multiscale linear transform, background extraction and
/// automatic gradient removal, each window run against the processes library, launched through the
/// registry and held to the recorded behaviour expectations. Runs under a headless QApplication whose event loop
/// delivers what a run's workers queue to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ProcessesMasks", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
