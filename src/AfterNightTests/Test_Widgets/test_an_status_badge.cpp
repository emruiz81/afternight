// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "components/AnStatusBadge.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnStatusBadge, AnStatusBadge_ShowsSuccess)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStatusBadge badge(QStringLiteral("Ready"));
    badge.setStatus(an::ui::AnStatusBadge::Status::Success);
    an::ui::refreshTokens();
    badge.refreshTheme();

    EXPECT_EQ(badge.palette().color(QPalette::WindowText), an::ui::tokens().colors.statusSuccess);
}

TEST(WidgetsComponentsAnStatusBadge, AnStatusBadge_ShowsWarning)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStatusBadge badge(QStringLiteral("Warning"));
    badge.setStatus(an::ui::AnStatusBadge::Status::Warning);
    an::ui::refreshTokens();
    badge.refreshTheme();

    EXPECT_EQ(badge.palette().color(QPalette::WindowText), an::ui::tokens().colors.statusWarning);
}

TEST(WidgetsComponentsAnStatusBadge, AnStatusBadge_ShowsError)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStatusBadge badge(QStringLiteral("Error"));
    badge.setStatus(an::ui::AnStatusBadge::Status::Error);
    an::ui::refreshTokens();
    badge.refreshTheme();

    EXPECT_EQ(badge.palette().color(QPalette::WindowText), an::ui::tokens().colors.statusError);
}

TEST(WidgetsComponentsAnStatusBadge, AnStatusBadge_ShowsInfo)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStatusBadge badge(QStringLiteral("Info"));
    badge.setStatus(an::ui::AnStatusBadge::Status::Info);
    an::ui::refreshTokens();
    badge.refreshTheme();

    EXPECT_EQ(badge.palette().color(QPalette::WindowText), an::ui::tokens().colors.statusInfo);
}

TEST(WidgetsComponentsAnStatusBadge, AnStatusBadge_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnStatusBadge badge(QStringLiteral("Ready"));
    badge.setStatus(an::ui::AnStatusBadge::Status::Success);

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    badge.refreshTheme();
    const QColor darkTextColor = badge.palette().color(QPalette::WindowText);

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    badge.refreshTheme();
    const QColor lightTextColor = badge.palette().color(QPalette::WindowText);

    EXPECT_TRUE(darkTextColor.isValid());
    EXPECT_TRUE(lightTextColor.isValid());
    EXPECT_NE(darkTextColor, lightTextColor);
}
