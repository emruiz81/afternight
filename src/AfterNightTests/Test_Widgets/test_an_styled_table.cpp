// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QHeaderView>

#include "components/AnStyledTable.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnStyledTable, AnStyledTable_HasThemedHeaders)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStyledTable table;
    an::ui::refreshTokens();
    table.refreshTheme();

    // Header styling is now via QPalette, not QSS.
    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->palette().color(QPalette::Window), an::ui::tokens().colors.bgCardPrimary);
    EXPECT_EQ(header->palette().color(QPalette::WindowText), an::ui::tokens().colors.textSecondary);
}

TEST(WidgetsComponentsAnStyledTable, AnStyledTable_SelectionColor)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStyledTable table;
    an::ui::refreshTokens();
    table.refreshTheme();

    // Selection highlight is now via QPalette::Highlight.
    QColor expectedHighlight = an::ui::tokens().colors.accentPrimary;
    expectedHighlight.setAlpha(an::ui::isDarkTheme() ? 116 : 84);
    EXPECT_EQ(table.palette().color(QPalette::Highlight), expectedHighlight);
}

TEST(WidgetsComponentsAnStyledTable, AnStyledTable_GridLines)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStyledTable table;
    an::ui::refreshTokens();
    table.refreshTheme();

    // Base/AlternateBase colors are set for grid appearance.
    EXPECT_EQ(table.palette().color(QPalette::Base), an::ui::tokens().colors.bgInput);
    EXPECT_EQ(table.palette().color(QPalette::AlternateBase), an::ui::tokens().colors.bgSurface);
}
