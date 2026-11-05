// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Channel Composition dialog as its builder makes it: its colour space, three components named red, green and
/// blue, Linear Match off, and Ok and Close.

#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QWidget>

#include "processes/ChannelCompositionProcessUiBuilder.h"

namespace
{

// The dialog opens as a window of its own, its three components named for red, green and blue and Linear Match off,
// with Ok and Close and no Reset.
TEST(ChannelCompositionProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildChannelCompositionProcessUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.colorSpace, nullptr);
    ASSERT_NE(ui.redChannel, nullptr);
    ASSERT_NE(ui.greenChannel, nullptr);
    ASSERT_NE(ui.blueChannel, nullptr);
    ASSERT_NE(ui.channel0Label, nullptr);
    ASSERT_NE(ui.channel1Label, nullptr);
    ASSERT_NE(ui.channel2Label, nullptr);
    ASSERT_NE(ui.linearMatchCheckBox, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Channel Composition"));
    EXPECT_EQ(host.windowModality(), Qt::NonModal);
    EXPECT_GE(host.minimumSize().width(), 420);
    EXPECT_GE(host.minimumSize().height(), 220);

    EXPECT_EQ(ui.channel0Label->text(), QStringLiteral("Red"));
    EXPECT_EQ(ui.channel1Label->text(), QStringLiteral("Green"));
    EXPECT_EQ(ui.channel2Label->text(), QStringLiteral("Blue"));
    EXPECT_EQ(ui.linearMatchCheckBox->text(), QStringLiteral("Linear Match"));
    EXPECT_FALSE(ui.linearMatchCheckBox->isChecked());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Ok), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_EQ(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

} // namespace
