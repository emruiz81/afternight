// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Test_EngineLibraryParity - entry point.
//
// The sweeps print one `parity |` line per case, so a run's log is the measurement record; nothing
// here needs the engine's storage tree.

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
