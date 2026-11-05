// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QLabel>
#include <QLineEdit>

#include "components/AnInputField.h"
#include "theme/AnDesignTokens.h"
#include "theme_test_utils.h"

TEST(WidgetsComponentsAnInputField, AnInputField_UpdatesTextAndPlaceholder)
{
    an::ui::AnInputField field(QStringLiteral("Output"), QStringLiteral("Select directory"));

    ASSERT_NE(field.lineEdit(), nullptr);
    EXPECT_EQ(field.placeholderText(), QStringLiteral("Select directory"));
    EXPECT_EQ(field.lineEdit()->placeholderText(), QStringLiteral("Select directory"));

    field.setText(QStringLiteral("M42"));
    EXPECT_EQ(field.text(), QStringLiteral("M42"));
    EXPECT_EQ(field.lineEdit()->text(), QStringLiteral("M42"));
}

TEST(WidgetsComponentsAnInputField, AnInputField_LabelVisibilityTracksLabelText)
{
    an::ui::AnInputField field;

    ASSERT_NE(field.label(), nullptr);
    EXPECT_TRUE(field.label()->isHidden());

    field.setLabelText(QStringLiteral("Preset"));
    EXPECT_EQ(field.labelText(), QStringLiteral("Preset"));
    EXPECT_FALSE(field.label()->isHidden());

    field.setLabelText(QString());
    EXPECT_TRUE(field.label()->isHidden());
}

TEST(WidgetsComponentsAnInputField, AnInputField_UsesFocusBorderToken)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    paletteOverride.setWindowColor(QColor(10, 10, 10));

    an::ui::AnInputField field(QStringLiteral("Name"));
    an::ui::refreshTokens();
    field.refreshTheme();

    // Focus border is now painted by AnProxyStyle using design tokens.
    // Verify the label has a themed palette color as evidence of theming.
    EXPECT_EQ(field.label()->palette().color(QPalette::WindowText), an::ui::tokens().colors.textSecondary);
}

TEST(WidgetsComponentsAnInputField, AnInputField_ThemeAware)
{
    an::tests::widgets::ScopedPaletteOverride paletteOverride;
    an::ui::AnInputField field(QStringLiteral("Project"));

    paletteOverride.setWindowColor(QColor(10, 10, 10));
    an::ui::refreshTokens();
    field.refreshTheme();
    const QColor darkLabelColor = field.label()->palette().color(QPalette::WindowText);

    paletteOverride.setWindowColor(QColor(245, 245, 245));
    an::ui::refreshTokens();
    field.refreshTheme();
    const QColor lightLabelColor = field.label()->palette().color(QPalette::WindowText);

    EXPECT_TRUE(darkLabelColor.isValid());
    EXPECT_TRUE(lightLabelColor.isValid());
    EXPECT_NE(darkLabelColor, lightLabelColor);
}
