// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>

#include "fixtures/HeadlessQtFixture.h"

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    an::tests::fixtures::prepareHeadlessQtEnvironment(argc, argv);
    an::tests::fixtures::installQtTestMessageFilters();

    QApplication app(argc, argv);
    app.setApplicationName("Test_Widgets");
    app.setOrganizationName("AfterNight");
    an::tests::fixtures::applyFusionStyle(app);

    return RUN_ALL_TESTS();
}
