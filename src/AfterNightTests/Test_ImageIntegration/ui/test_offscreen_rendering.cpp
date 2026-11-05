// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QGuiApplication>
#include <QString>

TEST(OffscreenRendering, IsActive)
{
    QString platform = QGuiApplication::platformName();
    // Usually "offscreen" or "minimal" depending on Qt version/plugin and platform.
#ifdef _WIN32
    // On Windows we default to "minimal" for stability unless the runner explicitly requests otherwise.
    EXPECT_TRUE(platform == QString("minimal") || platform == QString("offscreen"))
        << "Expected platform to be 'minimal' or 'offscreen', got: " << platform.toStdString();
#else
    EXPECT_EQ(platform, QString("offscreen")) << "Expected platform to be 'offscreen', got: " << platform.toStdString();
#endif
}
