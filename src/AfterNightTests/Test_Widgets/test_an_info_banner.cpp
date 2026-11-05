// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QLabel>

#include "components/AnInfoBanner.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnInfoBanner, AnInfoBanner_ShowsInfoTone)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnInfoBanner banner(QStringLiteral("Integration complete"), an::ui::AnInfoBanner::Tone::Info);
    an::ui::refreshTokens();
    banner.refreshTheme();

    // Tone color is applied to the message label via palette (WindowText role).
    const QColor msgColor = banner.messageLabel()->palette().color(QPalette::WindowText);
    EXPECT_EQ(msgColor, an::ui::tokens().colors.statusInfo);
}

TEST(WidgetsComponentsAnInfoBanner, AnInfoBanner_ShowsWarningAndErrorTones)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnInfoBanner banner(QStringLiteral("Low disk space"), an::ui::AnInfoBanner::Tone::Warning);
    an::ui::refreshTokens();
    banner.refreshTheme();

    EXPECT_EQ(banner.messageLabel()->palette().color(QPalette::WindowText), an::ui::tokens().colors.statusWarning);

    banner.setTone(an::ui::AnInfoBanner::Tone::Error);
    banner.refreshTheme();
    EXPECT_EQ(banner.messageLabel()->palette().color(QPalette::WindowText), an::ui::tokens().colors.statusError);
}

TEST(WidgetsComponentsAnInfoBanner, AnInfoBanner_UpdatesMessage)
{
    an::ui::AnInfoBanner banner;

    ASSERT_NE(banner.messageLabel(), nullptr);
    banner.setMessage(QStringLiteral("Frames imported"));
    EXPECT_EQ(banner.message(), QStringLiteral("Frames imported"));
    EXPECT_EQ(banner.messageLabel()->text(), QStringLiteral("Frames imported"));
}

TEST(WidgetsComponentsAnInfoBanner, AnInfoBanner_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnInfoBanner banner(QStringLiteral("Processing"));

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    banner.refreshTheme();
    const QColor darkMsgColor = banner.messageLabel()->palette().color(QPalette::WindowText);

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    banner.refreshTheme();
    const QColor lightMsgColor = banner.messageLabel()->palette().color(QPalette::WindowText);

    EXPECT_TRUE(darkMsgColor.isValid());
    EXPECT_TRUE(lightMsgColor.isValid());
    EXPECT_NE(darkMsgColor, lightMsgColor);
}
