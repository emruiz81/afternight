// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the optional Python adapter's suite.
//
// The suite links the engine, the adapter, and gtest. It is one of the two direct engine suites that
// link a second AfterNight library, because the adapter is a second library by construction - and
// that is also what the suite proves about the build, on top of what it proves about the behaviour:
// a consumer reaches the adapter by linking it, and the engine it sits beside knows nothing about it.
//
// Roughly half the cases run in either configuration of the adapter. The other half need a Python
// runtime, and they say so by skipping rather than by passing: a build regenerated with
// AFTERNIGHT_PYTHON=0 has no interpreter to start, and a case that quietly reported success there
// would be reporting that the absent path works when it had not run it.
//
// The cases themselves live one file per public header.

#include <gtest/gtest.h>

// No Qt and no storage-aware service is involved here, so a plain gtest main is all that is needed.
// Nothing in this suite writes a file, so there is no storage root to isolate either.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
