// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QLabel>
#include <QPushButton>

#include "components/AnSectionHeader.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnSectionHeader, AnSectionHeader_DisplaysTitle)
{
    an::ui::AnSectionHeader header(QStringLiteral("Stacking Settings"));

    ASSERT_NE(header.titleLabel(), nullptr);
    EXPECT_EQ(header.titleLabel()->text(), QStringLiteral("Stacking Settings"));
}

TEST(WidgetsComponentsAnSectionHeader, AnSectionHeader_DisplaysDescription)
{
    an::ui::AnSectionHeader header(QStringLiteral("Stacking"), QStringLiteral("Configure stacking quality options."));

    ASSERT_NE(header.descriptionLabel(), nullptr);
    EXPECT_EQ(header.descriptionLabel()->text(), QStringLiteral("Configure stacking quality options."));
    EXPECT_FALSE(header.descriptionLabel()->isHidden());
}

TEST(WidgetsComponentsAnSectionHeader, AnSectionHeader_OptionalAction)
{
    an::ui::AnSectionHeader header(QStringLiteral("Normalization"), QStringLiteral("Choose reference and fit options."),
                                   QStringLiteral("Configure"));

    QPushButton* button = header.actionButton();
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text(), QStringLiteral("Configure"));
    EXPECT_FALSE(button->isHidden());

    int clickCount = 0;
    QObject::connect(button, &QPushButton::clicked, [&clickCount]() { ++clickCount; });
    button->click();
    EXPECT_EQ(clickCount, 1);

    header.setActionText(QString());
    EXPECT_TRUE(button->isHidden());
}

TEST(WidgetsComponentsAnSectionHeader, AnSectionHeader_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnSectionHeader header(QStringLiteral("Registration"), QStringLiteral("Tune star detection and matching."),
                                   QStringLiteral("Advanced"));

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    header.refreshTheme();
    const QColor darkTitleColor = header.titleLabel()->palette().color(QPalette::WindowText);

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    header.refreshTheme();
    const QColor lightTitleColor = header.titleLabel()->palette().color(QPalette::WindowText);

    EXPECT_TRUE(darkTitleColor.isValid());
    EXPECT_TRUE(lightTitleColor.isValid());
    EXPECT_NE(darkTitleColor, lightTitleColor);
}
