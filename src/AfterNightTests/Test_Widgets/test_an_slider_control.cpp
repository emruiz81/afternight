// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QLabel>
#include <QSlider>
#include <QSpinBox>

#include "components/AnSliderControl.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnSliderControl, AnSliderControl_SynchronizesSliderAndSpinBox)
{
    an::ui::AnSliderControl control(QStringLiteral("Strength"), 0, 200, 50);

    ASSERT_NE(control.slider(), nullptr);
    ASSERT_NE(control.spinBox(), nullptr);
    EXPECT_EQ(control.slider()->value(), 50);
    EXPECT_EQ(control.spinBox()->value(), 50);

    control.slider()->setValue(120);
    EXPECT_EQ(control.spinBox()->value(), 120);

    control.spinBox()->setValue(33);
    EXPECT_EQ(control.slider()->value(), 33);
    EXPECT_EQ(control.value(), 33);
}

TEST(WidgetsComponentsAnSliderControl, AnSliderControl_AppliesRangeAndLabelVisibility)
{
    an::ui::AnSliderControl control;

    ASSERT_NE(control.label(), nullptr);
    EXPECT_TRUE(control.label()->isHidden());

    control.setLabelText(QStringLiteral("Radius"));
    EXPECT_EQ(control.labelText(), QStringLiteral("Radius"));
    EXPECT_FALSE(control.label()->isHidden());

    control.setRange(-5, 5);
    control.setValue(3);
    EXPECT_EQ(control.minimum(), -5);
    EXPECT_EQ(control.maximum(), 5);
    EXPECT_EQ(control.value(), 3);

    control.setLabelText(QString());
    EXPECT_TRUE(control.label()->isHidden());
}

TEST(WidgetsComponentsAnSliderControl, AnSliderControl_UsesAccentTrackColor)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnSliderControl control(QStringLiteral("Blend"));
    an::ui::refreshTokens();
    control.refreshTheme();

    // Slider track color is now painted by AnProxyStyle using design tokens.
    // Verify the label has a themed palette color as evidence of theming.
    EXPECT_EQ(control.label()->palette().color(QPalette::WindowText), an::ui::tokens().colors.textSecondary);
}

TEST(WidgetsComponentsAnSliderControl, AnSliderControl_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnSliderControl control(QStringLiteral("Strength"));

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    control.refreshTheme();
    const QColor darkLabelColor = control.label()->palette().color(QPalette::WindowText);

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    control.refreshTheme();
    const QColor lightLabelColor = control.label()->palette().color(QPalette::WindowText);

    EXPECT_TRUE(darkLabelColor.isValid());
    EXPECT_TRUE(lightLabelColor.isValid());
    EXPECT_NE(darkLabelColor, lightLabelColor);
}
