// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Entry point of the astrometry suite: the plate solve and its solvers, the catalogs it and the overlays read, the
/// catalog search and download services, the plate-solve and astrometry windows, and the overlays a view draws over a
/// solved image, compiled against the consumer runtime and the engine from an explicit list over the viewer's,
/// under a headless QApplication whose event loop delivers what the workers queue to the GUI thread. The settings the
/// catalog search keeps go into an INI file below the storage root.

#include "fixtures/harness/QtTestHarness.h"

#include <AfterNightEngine/Core/Version.h>

#include <QSettings>
#include <QString>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    an::tests::harness::QtTestHarness harness(argc, argv, {"Test_ControlsAstrometry", an::engineVersionString()});
    if (!harness.valid())
    {
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromStdU16String(harness.storage().root().u16string()));
    ::testing::InitGoogleTest(&argc, argv);
    return harness.finish(RUN_ALL_TESTS());
}
