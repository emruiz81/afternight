// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the spatial and noise processes' suite: the nineteen dialogs - the blurs, the median, the bilateral,
/// banding, NL-Bayes, total variation and cosmetic denoisers, the detail filters, the noise generator, the morphology,
/// the local contrast and histogram equalizers and the six that run a G'MIC command - run against the processes
/// library, which alone links the G'MIC adapter, each driven through its own widgets and held to the recorded
/// behaviour expectations. Runs under a headless QApplication whose event loop delivers what a process's workers queue
/// to the GUI thread.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ProcessesSpatial", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
