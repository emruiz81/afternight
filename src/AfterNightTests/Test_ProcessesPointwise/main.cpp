// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the pointwise and tone processes' suite: brightness and contrast, gamma, exposure, the arcsinh
/// stretch, colorize, colour saturation and the HSL and LCh adjustments, each window run against the processes library,
/// launched through the registry and held to the recorded behaviour expectations. Runs under a
/// headless QApplication whose event loop delivers what a run's workers queue to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ProcessesPointwise", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
