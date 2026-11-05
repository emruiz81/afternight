// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "processes/ImageIntegration/dialogs/ConfirmationDialogUtils.h"

TEST(ConfirmationDialog, ShowsForMoreThan10Frames)
{
    EXPECT_TRUE(ImageIntegration::shouldShowRemoveFramesConfirmation(11, false));
    EXPECT_FALSE(ImageIntegration::shouldShowRemoveFramesConfirmation(10, false));
    EXPECT_FALSE(ImageIntegration::shouldShowRemoveFramesConfirmation(5, false));
}

TEST(ConfirmationDialog, RespectsDontAskAgain)
{
    EXPECT_FALSE(ImageIntegration::shouldShowRemoveFramesConfirmation(100, true));
}

TEST(ConfirmationDialog, MessageListsFrameNamesAndUndoHint)
{
    const QStringList frames = {QStringLiteral("C:/data/frame1.fit"), QStringLiteral("C:/data/frame2.fit"),
                                QStringLiteral("C:/data/frame3.fit")};

    const QString msg = ImageIntegration::buildRemoveFramesConfirmationMessage(frames);
    EXPECT_TRUE(msg.contains("frame1.fit"));
    EXPECT_TRUE(msg.contains("frame2.fit"));
    EXPECT_TRUE(msg.contains("frame3.fit"));
    EXPECT_TRUE(msg.contains("Ctrl+Z"));
}
