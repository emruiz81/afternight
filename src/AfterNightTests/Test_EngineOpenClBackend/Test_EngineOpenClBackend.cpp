// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the opt-in OpenCL proof backend's suite.
//
// The suite links the engine, the backend and gtest, and exists only in a tree generated with
// AFTERNIGHT_OPENCL=1. It proves the backend on whatever GPU the host has, through the engine's checked
// `Device` facade, and it says so when the host has none: every case that needs a device skips with the
// runtime's own answer rather than passing. On the nominated host, AFTERNIGHT_TEST_REQUIRE_OPENCL=1 turns
// that skip into a failure, because there a skipped suite would be a proof that never ran.
//
// The cases live one file per concern: the runtime report and the refusals that come before a device
// exists, the device contract a mock also satisfies, and the lifecycle and cancellation rules that only
// an asynchronous device with a thread of its own exercises.

#include <gtest/gtest.h>

// No Qt and no storage-aware service is involved here, so a plain gtest main is all that is needed.
// Nothing in this suite writes a file, so there is no storage root to isolate either.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
