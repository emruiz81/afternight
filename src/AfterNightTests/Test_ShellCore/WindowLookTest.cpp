// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The look of the windows the shell core makes that stay windows under the Observatory profile.

#include "ShellCoreTestSupport.h"
#include "fixtures/WindowLook.h"

#include "actions/ProcessListDialogs.h"
#include "core/ApplicationCore.h"

#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <gtest/gtest.h>

namespace
{

using an::tests::shellcore::ScopedStorageRoot;
using an::ui::shell::ApplicationCore;

} // namespace

// Manage Favorite Processes, which the application runs to its answer: a heading that says what it is, the processes
// in a list, and a footer whose button that accepts is at the right - in both densities, within its screen.
TEST(WindowLook, ManageFavorites)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const ScopedStorageRoot storage(root.path());
    ApplicationCore core;
    core.openStores();
    const an::tests::look::WindowLookMeasured measured = an::tests::look::expectModalWindowLook(
        [&core]() { (void)an::ui::shell::manageFavoriteProcesses(core, nullptr); },
        [](const QWidget& window) { return window.objectName() == QStringLiteral("manageFavoriteProcessesDialog"); });
    core.closeStores();
    AN_WINDOW_LOOK_REPORT(measured);
}
