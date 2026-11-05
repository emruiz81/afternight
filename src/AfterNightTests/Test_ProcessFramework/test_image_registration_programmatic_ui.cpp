// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Registration dialog as its builder makes it: the widgets the window reads with their defaults, a frame
/// table several rows of which can be selected, the preview installed in its placeholder, the quality thresholds, and
/// the dialog made and let go repeatedly.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QWidget>
#include <memory>

#include "processes/ImageRegistrationProcessUiBuilder.h"
#include "processes/ProcessCommonUtils.h"

namespace
{

// The dialog opens with the widgets its window reads at their defaults and a frame table of which a person can select
// several rows at once, as the blink comparator and Remove Selected act on every frame selected.
TEST(ImageRegistrationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildImageRegistrationProcessUi(&host);

    ASSERT_NE(ui.verticalLayout_Main, nullptr);
    ASSERT_NE(ui.verticalLayoutTab1_Main, nullptr);
    ASSERT_NE(ui.verticalLayoutTab2_Main, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.tableLayout, nullptr);
    ASSERT_NE(ui.tableScrollArea, nullptr);
    ASSERT_NE(ui.tab1, nullptr);
    ASSERT_NE(ui.tab2, nullptr);

    ASSERT_NE(ui.frameListCard, nullptr);
    ASSERT_NE(ui.registrationGroupBox, nullptr);
    ASSERT_NE(ui.alignmentGroupBox, nullptr);
    ASSERT_NE(ui.outputDirectoryCard, nullptr);
    ASSERT_NE(ui.advancedCard, nullptr);

    ASSERT_NE(ui.imagePreviewWidget, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    ASSERT_NE(ui.addFiles, nullptr);
    ASSERT_NE(ui.addFolder, nullptr);
    ASSERT_NE(ui.removeSelected, nullptr);
    ASSERT_NE(ui.removeFailedImagesButton, nullptr);
    ASSERT_NE(ui.moveUpButton, nullptr);
    ASSERT_NE(ui.moveDownButton, nullptr);
    ASSERT_NE(ui.nextFailedButton, nullptr);
    ASSERT_NE(ui.blinkCompareButton, nullptr);
    ASSERT_NE(ui.clearAll, nullptr);
    ASSERT_NE(ui.currentImageLabel, nullptr);
    ASSERT_NE(ui.registeredImagesLabel, nullptr);
    ASSERT_NE(ui.referenceImageLabel, nullptr);
    ASSERT_NE(ui.showInfoLabel, nullptr);

    ASSERT_NE(ui.registrationMethodSelector, nullptr);
    ASSERT_NE(ui.referenceModeSelector, nullptr);
    ASSERT_NE(ui.referenceImageValue, nullptr);
    ASSERT_NE(ui.minStarParirsValue, nullptr);
    ASSERT_NE(ui.maxStarParirsValue, nullptr);
    ASSERT_NE(ui.transformationSelector, nullptr);
    ASSERT_NE(ui.advancedSettingsButton, nullptr);
    ASSERT_NE(ui.autoSolveUnsolvedFramesCheck, nullptr);
    ASSERT_NE(ui.reprocessAlreadyRegisteredmages, nullptr);
    ASSERT_NE(ui.clearRegisteredDataButton, nullptr);
    ASSERT_NE(ui.registerButton, nullptr);

    ASSERT_NE(ui.alignModeStandard, nullptr);
    ASSERT_NE(ui.alignModeMosaic, nullptr);
    ASSERT_NE(ui.alignModeIntersection, nullptr);
    ASSERT_NE(ui.interpolationSelector, nullptr);
    ASSERT_NE(ui.outputDirectory, nullptr);
    ASSERT_NE(ui.outputDirectoryButton, nullptr);
    ASSERT_NE(ui.alignButton, nullptr);

    ASSERT_NE(ui.qualityThresholdSection, nullptr);
    ASSERT_NE(ui.qualityMinStarsValue, nullptr);
    ASSERT_NE(ui.qualityMaxFwhmValue, nullptr);
    ASSERT_NE(ui.qualityMaxEccentricityValue, nullptr);
    ASSERT_NE(ui.applyThresholdsButton, nullptr);
    ASSERT_NE(ui.resetThresholdsButton, nullptr);
    ASSERT_NE(ui.chromaticAberrationCorrectionCheck, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image Registration"));
    EXPECT_EQ(host.minimumSize(), QSize(1200, 520));
    EXPECT_EQ(ui.imagePreviewWidget->minimumSize(), QSize(320, 220));
    EXPECT_EQ(ui.imagePreviewWidget->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    EXPECT_EQ(ui.tableWidget->minimumHeight(), 0);
    auto* hostLayout = qobject_cast<QVBoxLayout*>(host.layout());
    ASSERT_NE(hostLayout, nullptr);
    EXPECT_EQ(hostLayout->stretch(0), 3);
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
    EXPECT_LE(ui.registrationGroupBox->geometry().right(), settingsContentRight);
    EXPECT_LE(ui.alignmentGroupBox->geometry().right(), settingsContentRight);
    EXPECT_LE(ui.outputDirectoryCard->geometry().right(), settingsContentRight);
    auto* frameListScrollArea = host.findChild<QScrollArea*>(QStringLiteral("frameListScrollArea"));
    ASSERT_NE(frameListScrollArea, nullptr);
    EXPECT_EQ(hostLayout->indexOf(frameListScrollArea), 1);
    EXPECT_EQ(hostLayout->stretch(1), 2);
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
    EXPECT_EQ(ui.showInfoLabel->textFormat(), Qt::PlainText);
    EXPECT_TRUE(ui.showInfoLabel->wordWrap());
    EXPECT_TRUE(ui.outputDirectory->isReadOnly());
    EXPECT_EQ(ui.registerButton->minimumHeight(), 50);
    EXPECT_EQ(ui.alignButton->minimumHeight(), 50);

    EXPECT_EQ(ui.minStarParirsValue->minimum(), 5);
    EXPECT_EQ(ui.minStarParirsValue->maximum(), 10000);
    EXPECT_EQ(ui.minStarParirsValue->value(), 10);
    EXPECT_EQ(ui.maxStarParirsValue->minimum(), 10);
    EXPECT_EQ(ui.maxStarParirsValue->maximum(), 20000);
    EXPECT_EQ(ui.maxStarParirsValue->value(), 2000);
    EXPECT_GE(ui.referenceModeSelector->minimumWidth(), 112);
    EXPECT_EQ(ui.referenceImageValue->minimum(), 1);
    EXPECT_EQ(ui.referenceImageValue->maximum(), 100000);

    EXPECT_EQ(ui.qualityMinStarsValue->minimum(), 0);
    EXPECT_EQ(ui.qualityMinStarsValue->maximum(), 100000);
    EXPECT_EQ(ui.qualityMinStarsValue->value(), 0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxFwhmValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxFwhmValue->maximum(), 999.0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxFwhmValue->singleStep(), 0.1);
    EXPECT_DOUBLE_EQ(ui.qualityMaxFwhmValue->value(), 0.0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxEccentricityValue->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxEccentricityValue->maximum(), 1.0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxEccentricityValue->singleStep(), 0.01);
    EXPECT_DOUBLE_EQ(ui.qualityMaxEccentricityValue->value(), 0.0);

    EXPECT_TRUE(ui.alignModeStandard->isChecked());
    EXPECT_FALSE(ui.alignModeMosaic->isChecked());
    EXPECT_FALSE(ui.alignModeIntersection->isChecked());
    EXPECT_FALSE(ui.qualityThresholdSection->isExpanded());
    EXPECT_FALSE(ui.autoSolveUnsolvedFramesCheck->isChecked());
    EXPECT_FALSE(ui.chromaticAberrationCorrectionCheck->isChecked());
    EXPECT_FALSE(ui.reprocessAlreadyRegisteredmages->isChecked());

    EXPECT_EQ(ui.registerButton->property("anButtonVariant").toString(), QStringLiteral("primary"));
    EXPECT_EQ(ui.alignButton->property("anButtonVariant").toString(), QStringLiteral("primary"));
    EXPECT_EQ(ui.clearRegisteredDataButton->property("anButtonVariant").toString(), QStringLiteral("danger"));
    EXPECT_EQ(ui.removeFailedImagesButton->property("anButtonVariant").toString(), QStringLiteral("danger"));
    EXPECT_EQ(ui.clearAll->property("anButtonVariant").toString(), QStringLiteral("danger"));
    EXPECT_EQ(ui.addFiles->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.addFolder->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.outputDirectoryButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.advancedSettingsButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.nextFailedButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.blinkCompareButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_TRUE(ui.showInfoLabel->property("anStatusTone").toString().isEmpty());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);

    EXPECT_EQ(host.findChild<QTabWidget*>(QStringLiteral("tabWidget")), nullptr);
    EXPECT_EQ(ui.tableWidget->selectionMode(), QAbstractItemView::ExtendedSelection);
}

// A preview installed in its placeholder fills it: it takes the placeholder's minimum size, grows with it, and leaves
// the placeholder where it was in the dialog.
TEST(ImageRegistrationProgrammaticUiTest, PreviewPlaceholderInstallsPreviewInsideStableHost)
{
    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    QWidget* placeholder = new QWidget(&host);
    placeholder->setMinimumSize(QSize(320, 220));
    layout->addWidget(placeholder);
    auto* preview = new QWidget(&host);

    process_utils::installPreviewInPlaceholder(placeholder, preview);

    ASSERT_NE(placeholder, nullptr);
    ASSERT_NE(placeholder->layout(), nullptr);
    EXPECT_EQ(layout->indexOf(placeholder), 0);
    EXPECT_EQ(preview->parentWidget(), placeholder);
    EXPECT_EQ(placeholder->layout()->indexOf(preview), 0);
    EXPECT_EQ(preview->minimumSize(), QSize(320, 220));
    EXPECT_EQ(preview->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
    EXPECT_EQ(preview->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
}

// The quality thresholds open folded with no threshold set, and hold what a person types.
TEST(ImageRegistrationProgrammaticUiTest, QualityThresholdControlsDefaultContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildImageRegistrationProcessUi(&host);

    EXPECT_FALSE(ui.qualityThresholdSection->isExpanded());
    EXPECT_EQ(ui.qualityMinStarsValue->value(), 0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxFwhmValue->value(), 0.0);
    EXPECT_DOUBLE_EQ(ui.qualityMaxEccentricityValue->value(), 0.0);

    ui.qualityMinStarsValue->setValue(42);
    ui.qualityMaxFwhmValue->setValue(3.2);
    ui.qualityMaxEccentricityValue->setValue(0.18);

    EXPECT_EQ(ui.qualityMinStarsValue->value(), 42);
    EXPECT_DOUBLE_EQ(ui.qualityMaxFwhmValue->value(), 3.2);
    EXPECT_DOUBLE_EQ(ui.qualityMaxEccentricityValue->value(), 0.18);
}

// The dialog can be made and let go again and again, each time whole.
TEST(ImageRegistrationProgrammaticUiTest, UiLifecycleRepeatedCreateDestroy)
{
    for (int i = 0; i < 3; ++i)
    {
        auto host = std::make_unique<QWidget>();
        auto ui = an::ui::processes::buildImageRegistrationProcessUi(host.get());
        ASSERT_NE(ui.frameListCard, nullptr);
        ASSERT_NE(ui.tableWidget, nullptr);
        ASSERT_NE(ui.buttonBox, nullptr);
    }
}

} // anonymous namespace
