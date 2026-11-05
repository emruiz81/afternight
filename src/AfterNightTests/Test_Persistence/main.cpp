// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QtGlobal>

#include "controls/PersistenceDbExecutor.h"
#include <AfterNightPlatform/Logging.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    QCoreApplication app(argc, argv);
    app.setApplicationName("Test_Persistence");
    app.setOrganizationName("AfterNight");

    if (!an::platform::isLoggingInitialized())
    {
        an::platform::initializeLogging("test_persistence.log", 1, 1, 100, an::platform::LogLevel::Error);
    }

    return RUN_ALL_TESTS();
}
