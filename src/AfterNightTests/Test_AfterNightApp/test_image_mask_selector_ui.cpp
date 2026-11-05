// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// Mask selector dialog modernization tests.
#include <gtest/gtest.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include "controls/ImageMaskSelectorControlUiBuilder.h"

TEST(ImageMaskSelectorUiTest, Builder_UsesSectionHeaderAndBodyCardWithDescription)
{
    QDialog host;
    auto ui = an::ui::controls::buildImageMaskSelectorControlUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    host.setLayout(ui.verticalLayout);
    host.show();
    host.layout()->activate();

    ASSERT_NE(ui.sectionHeader, nullptr);
    ASSERT_NE(ui.bodyGroupBox, nullptr);
    ASSERT_NE(ui.bodyDescription, nullptr);
    ASSERT_NE(ui.currentMask, nullptr);
    ASSERT_NE(ui.maskSelector, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(560, 300));
    EXPECT_EQ(host.maximumSize(), QSize(560, 300));
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageMaskSelectorSectionHeader")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageMaskSelectorBodyGroupBox")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("imageMaskSelectorBodyDescription")), nullptr);
    EXPECT_EQ(ui.sectionHeader->title(), QStringLiteral("Mask Selector"));
    EXPECT_FALSE(ui.bodyDescription->text().trimmed().isEmpty());
    EXPECT_TRUE(ui.bodyDescription->wordWrap());
    EXPECT_TRUE(ui.currentMask->isReadOnly());

    auto* okButton = ui.buttonBox->button(QDialogButtonBox::Ok);
    ASSERT_NE(okButton, nullptr);
    EXPECT_EQ(okButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_TRUE(okButton->isDefault());

    const QRect headerRect = ui.sectionHeader->geometry();
    const QRect cardRect = ui.bodyGroupBox->geometry();
    const QRect buttonsRect = ui.buttonBox->geometry();
    EXPECT_LE(headerRect.bottom(), cardRect.top());
    EXPECT_LE(cardRect.bottom(), buttonsRect.top());
}
