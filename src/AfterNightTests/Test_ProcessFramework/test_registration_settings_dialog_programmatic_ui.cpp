// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration's Registration Settings dialog as its builder makes it: its reference row at the dialog's default
/// width, and its advanced panel.

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QPushButton>

#include "processes/ImageIntegration/controls/RegistrationSettingsDialogUiBuilder.h"

namespace
{

// At the dialog's default width a long reference path keeps the reference row in order - its label, the path, then Auto
// - with the path given the room left.
TEST(RegistrationSettingsDialogProgrammaticUiTest, ReferenceRowStaysStableAtDefaultWidth)
{
    QDialog host;
    auto ui = an::ui::imageintegration::buildRegistrationSettingsDialogUi(&host);

    ASSERT_NE(ui.registrationGroupBox, nullptr);
    ASSERT_NE(ui.label_referenceImage, nullptr);
    ASSERT_NE(ui.referenceImageComboBox, nullptr);
    ASSERT_NE(ui.autoAssignButton, nullptr);
    ASSERT_NE(ui.horizontalLayout_referenceImage, nullptr);

    ui.referenceImageComboBox->addItem(
        QStringLiteral("/very/long/path/that/used/to/push/the/reference/combo/layout/into/overlap/"
                       "with/other/text/when/the/dialog/was/not-resized/light_0000000001.fit"));

    host.resize(550, 570);
    host.show();
    QApplication::processEvents();
    if (host.layout())
    {
        host.layout()->activate();
    }

    const int labelIndex = ui.horizontalLayout_referenceImage->indexOf(ui.label_referenceImage);
    const int comboIndex = ui.horizontalLayout_referenceImage->indexOf(ui.referenceImageComboBox);
    const int autoIndex = ui.horizontalLayout_referenceImage->indexOf(ui.autoAssignButton);

    ASSERT_GE(labelIndex, 0);
    ASSERT_GE(comboIndex, 0);
    ASSERT_GE(autoIndex, 0);
    EXPECT_LT(labelIndex, comboIndex);
    EXPECT_LT(comboIndex, autoIndex);

    EXPECT_GT(ui.referenceImageComboBox->width(), 0);
    EXPECT_GT(ui.autoAssignButton->width(), 0);

    EXPECT_EQ(ui.referenceImageComboBox->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
    EXPECT_EQ(ui.referenceImageComboBox->sizeAdjustPolicy(), QComboBox::AdjustToMinimumContentsLengthWithIcon);
}

// The advanced panel owns its layout and shows its star detection and miscellaneous sections once it is shown.
TEST(RegistrationSettingsDialogProgrammaticUiTest, AdvancedPanelOwnsLayoutAndRendersSectionsWhenShown)
{
    QDialog host;
    auto ui = an::ui::imageintegration::buildRegistrationSettingsDialogUi(&host);

    ASSERT_NE(ui.advancedSettingsPanel, nullptr);
    ASSERT_NE(ui.verticalLayout_advancedPanel, nullptr);
    ASSERT_NE(ui.starDetectionGroupBox, nullptr);
    ASSERT_NE(ui.miscGroupBox, nullptr);

    EXPECT_EQ(ui.advancedSettingsPanel->layout(), ui.verticalLayout_advancedPanel);

    ui.advancedSettingsPanel->setVisible(true);
    host.show();
    QApplication::processEvents();
    if (host.layout())
    {
        host.layout()->activate();
    }

    EXPECT_TRUE(ui.starDetectionGroupBox->isVisibleTo(&host));
    EXPECT_TRUE(ui.miscGroupBox->isVisibleTo(&host));
}

} // anonymous namespace
