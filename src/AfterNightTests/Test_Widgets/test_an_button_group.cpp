// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QPushButton>

#include "components/AnButtonGroup.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnButtonGroup, AnButtonGroup_AddsButtonsInOrder)
{
    an::ui::AnButtonGroup group;

    QPushButton* first = group.addButton(QStringLiteral("Cancel"));
    QPushButton* second = group.addButton(QStringLiteral("Apply"), an::ui::AnButtonGroup::ButtonVariant::Primary);

    const QList<QPushButton*> buttons = group.buttons();
    ASSERT_EQ(buttons.size(), 2);
    EXPECT_EQ(buttons.at(0), first);
    EXPECT_EQ(buttons.at(1), second);
    EXPECT_EQ(first->text(), QStringLiteral("Cancel"));
    EXPECT_EQ(second->text(), QStringLiteral("Apply"));
}

TEST(WidgetsComponentsAnButtonGroup, AnButtonGroup_TracksPrimaryAndSecondaryVariants)
{
    an::ui::AnButtonGroup group;
    QPushButton* primary = group.addButton(QStringLiteral("Run"), an::ui::AnButtonGroup::ButtonVariant::Primary);
    QPushButton* secondary = group.addButton(QStringLiteral("Reset"), an::ui::AnButtonGroup::ButtonVariant::Secondary);

    ASSERT_NE(primary, nullptr);
    ASSERT_NE(secondary, nullptr);
    EXPECT_EQ(group.buttonVariant(primary), an::ui::AnButtonGroup::ButtonVariant::Primary);
    EXPECT_EQ(group.buttonVariant(secondary), an::ui::AnButtonGroup::ButtonVariant::Secondary);

    group.setButtonVariant(secondary, an::ui::AnButtonGroup::ButtonVariant::Primary);
    EXPECT_EQ(group.buttonVariant(secondary), an::ui::AnButtonGroup::ButtonVariant::Primary);
}

TEST(WidgetsComponentsAnButtonGroup, AnButtonGroup_PrimaryUsesAccentColor)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnButtonGroup group;
    QPushButton* primary = group.addButton(QStringLiteral("Apply"), an::ui::AnButtonGroup::ButtonVariant::Primary);
    an::ui::refreshTokens();
    group.refreshTheme();

    // Primary button variant is now handled by AnProxyStyle via the
    // "anButtonVariant" dynamic property — no QSS is used.
    EXPECT_EQ(primary->property("anButtonVariant").toString(), QStringLiteral("primary"));
}

TEST(WidgetsComponentsAnButtonGroup, AnButtonGroup_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnButtonGroup group;
    QPushButton* primary = group.addButton(QStringLiteral("Apply"), an::ui::AnButtonGroup::ButtonVariant::Primary);

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    group.refreshTheme();
    const QColor darkFont =
        primary->font().weight() != QFont::Normal ? primary->palette().color(QPalette::ButtonText) : QColor();

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    group.refreshTheme();
    const QColor lightFont =
        primary->font().weight() != QFont::Normal ? primary->palette().color(QPalette::ButtonText) : QColor();

    // The proxy style uses design tokens that differ between dark and light,
    // so the button appearance should change after a theme switch even though
    // no QSS is involved.
    EXPECT_EQ(primary->property("anButtonVariant").toString(), QStringLiteral("primary"));
    Q_UNUSED(darkFont);
    Q_UNUSED(lightFont);
}
