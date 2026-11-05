// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the external tools' suite: StarNet, StarNet's star mask and DeepSNR, each window run against the
/// processes library, launched through the registry and run over a stand-in for its program - this
/// executable itself, started by the product with the tool's arguments while `AFTERNIGHT_STANDIN_TOOL` names the
/// product it stands in for - and over the real programs where they are installed. Runs under a headless QApplication
/// whose event loop delivers what a run's workers queue to the GUI thread.

#include "Test_ProcessesExternalTools/StandInTool.h"

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <QtGlobal>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsSet(an::tests::processes::externaltools::kStandInToolVariable))
    {
        return an::tests::processes::externaltools::runStandInTool(argc, argv);
    }
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ProcessesExternalTools", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
