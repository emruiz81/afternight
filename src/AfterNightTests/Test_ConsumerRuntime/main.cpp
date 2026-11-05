// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the consumer runtime's suite.
///
/// The suite links the runtime, the engine and the platform library and nothing else of AfterNight's, constructs
/// no Qt object and writes no file: its cases hold images in memory only, and the runtime logs through the platform
/// library only once a process has initialized logging, which this one never does.

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
