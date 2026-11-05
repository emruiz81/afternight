// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The record every stand-in companion of this suite writes what the viewer asked it into, kept apart from the
/// stand-ins themselves.

#include "Test_ControlsSeams/ViewCompanionsStub.h"

namespace an::tests::viewer
{

CompanionRecord& companionRecord()
{
    static CompanionRecord record;
    return record;
}

void resetCompanionRecord()
{
    companionRecord() = CompanionRecord{};
}

} // namespace an::tests::viewer
