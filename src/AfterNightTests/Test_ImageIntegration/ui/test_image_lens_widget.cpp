// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>

#if defined(Q_OS_LINUX)
#include <QGuiApplication>
#endif

#include <QWidget>

#include "widgets/ImageLensWidget.h"

class ImageLensWidgetTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (!QApplication::instance())
        {
            static int argc = 0;
            static char* argv[] = {nullptr};
            new QApplication(argc, argv);
        }
    }
};

TEST_F(ImageLensWidgetTest, AttachToOwnerKeepsTopLevelToolWindow)
{
    bool isWayland = false;
#if defined(Q_OS_LINUX)
    isWayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive);
#endif

    QWidget owner;
    ImageLensWidget lens(nullptr);

    EXPECT_TRUE(lens.windowFlags().testFlag(Qt::FramelessWindowHint));
    if (isWayland)
    {
        EXPECT_FALSE(lens.windowFlags().testFlag(Qt::Tool));
    }
    else
    {
        EXPECT_TRUE(lens.windowFlags().testFlag(Qt::Tool));
        EXPECT_TRUE(lens.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    }

    lens.attachToOwner(&owner);

    if (isWayland)
    {
        EXPECT_EQ(lens.parentWidget(), owner.window());
        EXPECT_FALSE(lens.isWindow());
    }
    else
    {
        EXPECT_EQ(lens.parentWidget(), &owner);
        EXPECT_TRUE(lens.isWindow());
        EXPECT_TRUE(lens.windowFlags().testFlag(Qt::Tool));
    }

    lens.detachFromOwner();

    EXPECT_EQ(lens.parentWidget(), nullptr);
    if (!isWayland)
    {
        EXPECT_TRUE(lens.isWindow());
        EXPECT_TRUE(lens.windowFlags().testFlag(Qt::Tool));
    }
}
