// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Stacking dialog as its builder makes it: the widgets the window reads with their defaults and ranges, the
/// dialog made and let go repeatedly, and its scrollable cards.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

#include "processes/ImageStackingProcessUiBuilder.h"

namespace
{

// The dialog opens with the widgets its window reads at their defaults, the rejection thresholds held above zero, as
// every rejection reads a positive threshold.
TEST(ImageStackingProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    const auto ui = an::ui::processes::buildImageStackingProcessUi(&host);

    ASSERT_NE(ui.verticalLayout_Main, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.verticalLayout_5, nullptr);
    ASSERT_NE(ui.frameTableLayout, nullptr);
    ASSERT_NE(ui.groupTableLayout, nullptr);
    ASSERT_NE(ui.stackingSettingsCard, nullptr);
    ASSERT_NE(ui.outputCard, nullptr);
    ASSERT_NE(ui.frameGroupsCard, nullptr);
    ASSERT_NE(ui.frameListCard, nullptr);
    ASSERT_NE(ui.advancedCard, nullptr);
    ASSERT_NE(ui.imagePreviewWidget, nullptr);
    ASSERT_NE(ui.tableScrollArea, nullptr);
    ASSERT_NE(ui.groupTableScrollArea, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.groupTable, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    ASSERT_NE(ui.stackMethodSelector, nullptr);
    ASSERT_NE(ui.weightingSelector, nullptr);
    ASSERT_NE(ui.normalizationMode, nullptr);
    ASSERT_NE(ui.scaleEstimatorSelector, nullptr);
    ASSERT_NE(ui.rejectionMethodSelector, nullptr);
    ASSERT_NE(ui.sigmaLowValue, nullptr);
    ASSERT_NE(ui.sigmaHighValue, nullptr);
    EXPECT_DOUBLE_EQ(ui.sigmaLowValue->minimum(), 0.001);
    EXPECT_DOUBLE_EQ(ui.sigmaHighValue->minimum(), 0.001);
    ASSERT_NE(ui.groupByFilter, nullptr);
    ASSERT_NE(ui.groupBySession, nullptr);
    ASSERT_NE(ui.groupByExposure, nullptr);
    ASSERT_NE(ui.exposureTolerancePercentValue, nullptr);
    ASSERT_NE(ui.outputDirectoryEdit, nullptr);
    ASSERT_NE(ui.outputDirectoryButton, nullptr);
    ASSERT_NE(ui.outputImageNameEdit, nullptr);
    ASSERT_NE(ui.openAfterStackCheck, nullptr);
    ASSERT_NE(ui.sumExposureTimesCheckBox, nullptr);
    ASSERT_NE(ui.process64BitsImageCheckbox, nullptr);
    ASSERT_NE(ui.createRejectionMapsCheck, nullptr);
    ASSERT_NE(ui.mergeRejectionMapsCheck, nullptr);
    ASSERT_NE(ui.rejectionMapsPathLabel, nullptr);
    ASSERT_NE(ui.weightingBanner, nullptr);
    ASSERT_NE(ui.localNormalizationBanner, nullptr);
    ASSERT_NE(ui.localNormalizationConfigureButton, nullptr);
    ASSERT_NE(ui.rejectionSection, nullptr);
    ASSERT_NE(ui.limitMemoryUsageGroupBox, nullptr);
    ASSERT_NE(ui.maxMemoryMBValue, nullptr);
    ASSERT_NE(ui.debayerCFAGroupBox, nullptr);
    ASSERT_NE(ui.cfaPattern, nullptr);
    ASSERT_NE(ui.debayerMethod, nullptr);
    ASSERT_NE(ui.stackImagesButton, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image Stacking"));
    EXPECT_EQ(host.minimumSize(), QSize(1200, 520));
    EXPECT_EQ(ui.imagePreviewWidget->minimumSize(), QSize(320, 220));
    EXPECT_EQ(ui.imagePreviewWidget->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageSummaryBanner")), nullptr);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageSummaryDetailsWidget")), nullptr);
    for (const auto* widget : host.findChildren<QWidget*>())
    {
        const auto* card = dynamic_cast<const an::ui::AnCardWidget*>(widget);
        if (card)
        {
            EXPECT_NE(card->title(), QStringLiteral("Selected Frame"));
        }
    }
    EXPECT_EQ(ui.tableWidget->minimumHeight(), 0);
    EXPECT_EQ(ui.groupTable->minimumHeight(), 0);
    EXPECT_TRUE(ui.outputDirectoryEdit->isReadOnly());
    EXPECT_TRUE(ui.outputDirectoryButton->isEnabled());
    EXPECT_EQ(ui.stackImagesButton->minimumHeight(), 50);
    EXPECT_EQ(ui.tableScrollArea->minimumHeight(), 0);
    EXPECT_EQ(ui.tableScrollArea->sizePolicy().verticalPolicy(), QSizePolicy::Ignored);
    EXPECT_EQ(ui.groupTableScrollArea->minimumHeight(), 0);
    EXPECT_EQ(ui.groupTableScrollArea->sizePolicy().verticalPolicy(), QSizePolicy::Ignored);

    EXPECT_TRUE(ui.groupByFilter->isChecked());
    EXPECT_FALSE(ui.groupBySession->isChecked());
    EXPECT_FALSE(ui.groupByExposure->isChecked());
    EXPECT_DOUBLE_EQ(ui.exposureTolerancePercentValue->value(), 5.0);
    EXPECT_EQ(ui.exposureTolerancePercentValue->suffix(), QStringLiteral("%"));
    EXPECT_TRUE(ui.openAfterStackCheck->isChecked());
    EXPECT_TRUE(ui.limitMemoryUsageGroupBox->isCheckable());
    EXPECT_TRUE(ui.debayerCFAGroupBox->isCheckable());
    EXPECT_FALSE(ui.debayerCFAGroupBox->isChecked());
    EXPECT_EQ(ui.maxMemoryMBValue->minimum(), 256);
    EXPECT_EQ(ui.maxMemoryMBValue->maximum(), 8184);
    EXPECT_EQ(ui.maxMemoryMBValue->value(), 1024);
    EXPECT_EQ(ui.maxMemoryMBValue->suffix(), QStringLiteral("MB"));
    EXPECT_FALSE(ui.createRejectionMapsCheck->isChecked());
    EXPECT_FALSE(ui.mergeRejectionMapsCheck->isChecked());
    EXPECT_FALSE(ui.weightingBanner->isVisible());
    EXPECT_FALSE(ui.localNormalizationBanner->isVisible());
    EXPECT_FALSE(ui.localNormalizationConfigureButton->isVisible());
    EXPECT_FALSE(ui.rejectionSection->isExpanded());

    EXPECT_EQ(ui.stackImagesButton->property("anButtonVariant").toString(), QStringLiteral("primary"));
    EXPECT_EQ(ui.clearAll->property("anButtonVariant").toString(), QStringLiteral("danger"));
    EXPECT_EQ(ui.addFiles->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.removeSelected->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.outputDirectoryButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.localNormalizationConfigureButton->property("anButtonVariant").toString(),
              QStringLiteral("secondary"));

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// The dialog can be made and let go again and again, each time whole.
TEST(ImageStackingProgrammaticUiTest, UiLifecycleRepeatedCreateDestroy)
{
    for (int i = 0; i < 3; ++i)
    {
        auto host = std::make_unique<QWidget>();
        auto ui = an::ui::processes::buildImageStackingProcessUi(host.get());
        ASSERT_NE(ui.tableWidget, nullptr);
        ASSERT_NE(ui.groupTable, nullptr);
        ASSERT_NE(ui.buttonBox, nullptr);
    }
}

// The settings and the frame lists scroll in cards of their own, the settings kept inside their scroll area's width at
// the dialog's minimum size, with the buttons below them.
TEST(ImageStackingProgrammaticUiTest, RootAndSectionLayoutsUseScrollableCardStructure)
{
    QWidget host;
    auto ui = an::ui::processes::buildImageStackingProcessUi(&host);

    auto* hostLayout = qobject_cast<QVBoxLayout*>(host.layout());
    ASSERT_NE(hostLayout, nullptr);
    ASSERT_EQ(hostLayout->count(), 3);

    auto* topSection = hostLayout->itemAt(0)->layout();
    ASSERT_NE(topSection, nullptr);

    auto* settingsScrollArea = host.findChild<QScrollArea*>(QStringLiteral("settingsScrollArea"));
    ASSERT_NE(settingsScrollArea, nullptr);
    EXPECT_EQ(settingsScrollArea->minimumHeight(), 0);
    EXPECT_EQ(settingsScrollArea->minimumWidth(), 360);
    EXPECT_EQ(settingsScrollArea->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    ASSERT_NE(settingsScrollArea->widget(), nullptr);
    ASSERT_NE(settingsScrollArea->widget()->layout(), nullptr);
    const auto settingsMargins = settingsScrollArea->widget()->layout()->contentsMargins();
    EXPECT_EQ(settingsMargins.left(), an::ui::tokens().spacing.xs);
    EXPECT_EQ(settingsMargins.right(), settingsMargins.left());
    EXPECT_EQ(settingsScrollArea->widget()->minimumWidth(), 0);
    EXPECT_EQ(settingsScrollArea->widget()->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
    host.resize(host.minimumSize());
    host.show();
    QApplication::processEvents();
    ASSERT_GT(settingsScrollArea->viewport()->width(), 0);
    EXPECT_LE(settingsScrollArea->widget()->width(), settingsScrollArea->viewport()->width());
    const int settingsContentRight = settingsScrollArea->widget()->rect().right() - settingsMargins.right();
    EXPECT_LE(ui.stackingSettingsCard->geometry().right(), settingsContentRight);
    EXPECT_LE(ui.outputCard->geometry().right(), settingsContentRight);

    auto* rootScrollArea = host.findChild<QScrollArea*>(QStringLiteral("frameListScrollArea"));
    ASSERT_NE(rootScrollArea, nullptr);
    EXPECT_EQ(hostLayout->indexOf(rootScrollArea), 1);
    EXPECT_EQ(hostLayout->indexOf(ui.buttonBox), 2);

    EXPECT_EQ(ui.verticalLayout_2->indexOf(ui.imagePreviewWidget), 0);
    EXPECT_EQ(ui.imagePreviewWidget->parentWidget(), &host);
    ASSERT_NE(settingsScrollArea->widget(), nullptr);
    EXPECT_EQ(ui.stackingSettingsCard->parentWidget(), settingsScrollArea->widget());
    EXPECT_EQ(ui.outputCard->parentWidget(), settingsScrollArea->widget());
    EXPECT_EQ(ui.frameListCard->parentWidget(), rootScrollArea->widget());
    EXPECT_EQ(ui.frameGroupsCard->parentWidget(), rootScrollArea->widget());
    EXPECT_EQ(ui.advancedCard->parentWidget(), rootScrollArea->widget());
    EXPECT_EQ(ui.advancedCard->contentLayout(), ui.verticalLayout_5);
    EXPECT_EQ(ui.frameListCard->contentLayout(), ui.frameTableLayout);
    EXPECT_EQ(ui.frameGroupsCard->title(), QStringLiteral("Frame Groups"));
    EXPECT_EQ(ui.outputCard->title(), QStringLiteral("Output"));
}

} // anonymous namespace
