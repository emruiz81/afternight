// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>

#include <string>

#include "fixtures/HeadlessQtFixture.h"

#include <AfterNightPlatform/Logging.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    an::tests::fixtures::prepareHeadlessQtEnvironment(argc, argv);
    an::tests::fixtures::installQtTestMessageFilters();

    QApplication app(argc, argv);
    app.setApplicationName("Test_ThemeSystem");
    app.setOrganizationName("AfterNight");

    // The log's ring buffer alone, no file: the cases read what the theme logs.
    an::platform::initializeLogging(std::string());

    const int result = RUN_ALL_TESTS();
    an::platform::shutdownLogging();
    return result;
}
