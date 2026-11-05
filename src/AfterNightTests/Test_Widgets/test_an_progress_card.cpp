// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QLabel>
#include <QProgressBar>

#include "components/AnProgressCard.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnProgressCard, AnProgressCard_UpdatesPercentageLabelFromProgress)
{
    an::ui::AnProgressCard card(QStringLiteral("Integration Progress"));
    card.setProgressRange(0, 200);
    card.setProgressValue(50);

    ASSERT_NE(card.percentageLabel(), nullptr);
    EXPECT_EQ(card.progressBar()->value(), 50);
    EXPECT_EQ(card.percentageLabel()->text(), QStringLiteral("25%"));
}

TEST(WidgetsComponentsAnProgressCard, AnProgressCard_ShowsEtaText)
{
    an::ui::AnProgressCard card(QStringLiteral("Calibration"));

    ASSERT_NE(card.etaLabel(), nullptr);
    EXPECT_TRUE(card.etaLabel()->isHidden());

    card.setEtaText(QStringLiteral("ETA 00:42"));
    EXPECT_EQ(card.etaText(), QStringLiteral("ETA 00:42"));
    EXPECT_FALSE(card.etaLabel()->isHidden());

    card.setEtaText(QString());
    EXPECT_TRUE(card.etaLabel()->isHidden());
}

TEST(WidgetsComponentsAnProgressCard, AnProgressCard_HasCardStyling)
{
    an::ui::AnProgressCard card(QStringLiteral("Stacking"));
    card.refreshTheme();

    // Card styling is now handled via custom paintEvent and QPalette — no QSS.
    // Verify the title label has a themed palette color.
    ASSERT_NE(card.percentageLabel(), nullptr);
    EXPECT_TRUE(card.percentageLabel()->palette().color(QPalette::WindowText).isValid());
}

TEST(WidgetsComponentsAnProgressCard, AnProgressCard_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnProgressCard card(QStringLiteral("Registration"));

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    card.refreshTheme();
    const QColor darkTextColor = card.percentageLabel()->palette().color(QPalette::WindowText);

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    card.refreshTheme();
    const QColor lightTextColor = card.percentageLabel()->palette().color(QPalette::WindowText);

    EXPECT_TRUE(darkTextColor.isValid());
    EXPECT_TRUE(lightTextColor.isValid());
    EXPECT_NE(darkTextColor, lightTextColor);
}
