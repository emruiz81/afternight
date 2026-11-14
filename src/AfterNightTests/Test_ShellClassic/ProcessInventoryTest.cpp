// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The process registry as the application's own main window leaves it: every process of the application with the name
// the menu gives it, its category, how it is launched, what a launch asks of the active view and the page of the user
// guide it states, compared with the record beside this suite. The window is made first, because a descriptor is
// named by the menu entry that binds it.

#include "ApplicationDriving.h"
#include "InventoryRecords.h"

#include <QString>

#include <gtest/gtest.h>

// The registry's cases run over the application's own window, made once for them.
class ProcessRegistry : public an::tests::app::RealWindowCase
{
};

// Every process the application registers is the one the record holds, in the registry's order: its id, its names,
// its category, its launch mode, its menu action, its guards, its outputs and its help page. A process renamed,
// moved to another category, retired or added fails here until the record's entry says so.
TEST_F(ProcessRegistry, InventoryMatchesTheRecord)
{
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kProcessInventoryPath),
                                               an::tests::app::processInventoryPreamble());

    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kProcessesSection),
                               an::tests::app::processInventoryEntries()));
}
