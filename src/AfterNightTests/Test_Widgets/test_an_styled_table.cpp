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

    // The header is the third surface, in the secondary text.
    QHeaderView* header = table.horizontalHeader();
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->palette().color(QPalette::Window), an::ui::tokens().layers.surface3);
    EXPECT_EQ(header->palette().color(QPalette::WindowText), an::ui::tokens().colors.textSecondary);
}

TEST(WidgetsComponentsAnStyledTable, AnStyledTable_SelectionColor)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStyledTable table;
    an::ui::refreshTokens();
    table.refreshTheme();

    // The selection is the accent's soft share, under the primary text.
    EXPECT_EQ(table.palette().color(QPalette::Highlight), an::ui::tokens().colors.accentSoft);
    EXPECT_EQ(table.palette().color(QPalette::HighlightedText), an::ui::tokens().colors.textPrimary);
}

TEST(WidgetsComponentsAnStyledTable, AnStyledTable_GridLines)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnStyledTable table;
    an::ui::refreshTokens();
    table.refreshTheme();

    // The rows are on the first surface, and every other one is a breath lighter: the state colour's 4 % over it.
    const auto& tokenSet = an::ui::tokens();
    QColor alternate = tokenSet.colors.stateLayer;
    alternate.setAlphaF(0.04f);
    EXPECT_EQ(table.palette().color(QPalette::Base), tokenSet.layers.surface1);
    EXPECT_EQ(table.palette().color(QPalette::AlternateBase),
              an::ui::compositeOver(alternate, tokenSet.layers.surface1));
    EXPECT_NE(table.palette().color(QPalette::AlternateBase), table.palette().color(QPalette::Base));
}
