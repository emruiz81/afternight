// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Calibration dialog as its builder makes it: the widgets the window reads with their defaults, the output
/// formats it offers as a run reads them, the output options it opens at, and the dialog made and let go repeatedly.

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QSize>
#include <QSizePolicy>
#include <QWidget>
#include <memory>

#include "processes/CalibrationBatch.h"
#include "processes/ImageCalibrationProcessUiBuilder.h"

namespace
{

// The dialog opens with its preview room, read-only master and output paths, every master and the cosmetic correction
// unchecked at three sigmas, no dark optimization, unnormalized flats, the light's own name with "_cal" in Float32
// FITS and no pedestal, its advanced sections folded, and its buttons in their variants.
TEST(ImageCalibrationProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildImageCalibrationProcessUi(&host);

    ASSERT_NE(ui.verticalLayout_Main, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.verticalLayout_3, nullptr);
    ASSERT_NE(ui.tableLayout, nullptr);
    ASSERT_NE(ui.tableScrollArea, nullptr);
    ASSERT_NE(ui.outputCard, nullptr);
    ASSERT_NE(ui.frameListCard, nullptr);
    ASSERT_NE(ui.imagePreviewWidget, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.addFiles, nullptr);
    ASSERT_NE(ui.addFolder, nullptr);
    ASSERT_NE(ui.removeSelected, nullptr);
    ASSERT_NE(ui.moveUpButton, nullptr);
    ASSERT_NE(ui.moveDownButton, nullptr);
    ASSERT_NE(ui.currentImageLabel, nullptr);
    ASSERT_NE(ui.totalImagesLabel, nullptr);
    ASSERT_NE(ui.clearAll, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.biasGroupBox, nullptr);
    ASSERT_NE(ui.biasFilePath, nullptr);
    ASSERT_NE(ui.biasFileButton, nullptr);
    ASSERT_NE(ui.darkGroupBox, nullptr);
    ASSERT_NE(ui.darkFilePath, nullptr);
    ASSERT_NE(ui.darkFileButton, nullptr);
    ASSERT_NE(ui.darkOptimizationCheck, nullptr);
    ASSERT_NE(ui.cosmeticCorrectionGroupBox, nullptr);
    ASSERT_NE(ui.coldSigmaCheck, nullptr);
    ASSERT_NE(ui.coldSigmaValue, nullptr);
    ASSERT_NE(ui.hotSigmaCheck, nullptr);
    ASSERT_NE(ui.hotSigmaValue, nullptr);
    ASSERT_NE(ui.estimateCosmeticCorrectionButton, nullptr);
    ASSERT_NE(ui.darkAdvancedSection, nullptr);
    ASSERT_NE(ui.darkMultiplicationFactorValue, nullptr);
    ASSERT_NE(ui.flatGroupBox, nullptr);
    ASSERT_NE(ui.flatFilePath, nullptr);
    ASSERT_NE(ui.flatFileButton, nullptr);
    ASSERT_NE(ui.normalizeFlatsCheck, nullptr);
    ASSERT_NE(ui.flatAdvancedSection, nullptr);
    ASSERT_NE(ui.outputDirectory, nullptr);
    ASSERT_NE(ui.outputDirectoryButton, nullptr);
    ASSERT_NE(ui.preserveOriginalNameCheck, nullptr);
    ASSERT_NE(ui.filenameSuffixEdit, nullptr);
    ASSERT_NE(ui.outputFormatSelector, nullptr);
    ASSERT_NE(ui.outputPrecisionLabel, nullptr);
    ASSERT_NE(ui.outputPrecisionSelector, nullptr);
    ASSERT_NE(ui.outputAdvancedSection, nullptr);
    ASSERT_NE(ui.pedestalValue, nullptr);
    ASSERT_NE(ui.calibrateImagesButton, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image Calibration"));
    EXPECT_EQ(host.minimumSize(), QSize(1200, 520));

    EXPECT_EQ(ui.imagePreviewWidget->minimumSize(), QSize(320, 220));
    EXPECT_EQ(ui.imagePreviewWidget->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    EXPECT_EQ(ui.tableWidget->minimumHeight(), 0);
    EXPECT_TRUE(ui.biasFilePath->isReadOnly());
    EXPECT_TRUE(ui.darkFilePath->isReadOnly());
    EXPECT_TRUE(ui.flatFilePath->isReadOnly());
    EXPECT_TRUE(ui.outputDirectory->isReadOnly());
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageCalibrationSummaryCard")), nullptr);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageSummaryBanner")), nullptr);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageSummaryDetailsWidget")), nullptr);

    EXPECT_TRUE(ui.biasGroupBox->isCheckable());
    EXPECT_FALSE(ui.biasGroupBox->isChecked());
    EXPECT_TRUE(ui.darkGroupBox->isCheckable());
    EXPECT_FALSE(ui.darkGroupBox->isChecked());
    EXPECT_TRUE(ui.flatGroupBox->isCheckable());
    EXPECT_FALSE(ui.flatGroupBox->isChecked());

    EXPECT_TRUE(ui.cosmeticCorrectionGroupBox->isCheckable());
    EXPECT_FALSE(ui.cosmeticCorrectionGroupBox->isChecked());
    EXPECT_EQ(ui.coldSigmaValue->decimals(), 3);
    EXPECT_DOUBLE_EQ(ui.coldSigmaValue->singleStep(), 0.1);
    EXPECT_DOUBLE_EQ(ui.coldSigmaValue->value(), 3.0);
    EXPECT_EQ(ui.hotSigmaValue->decimals(), 3);
    EXPECT_DOUBLE_EQ(ui.hotSigmaValue->singleStep(), 0.1);
    EXPECT_DOUBLE_EQ(ui.hotSigmaValue->value(), 3.0);
    EXPECT_FALSE(ui.darkOptimizationCheck->isChecked());
    EXPECT_DOUBLE_EQ(ui.darkMultiplicationFactorValue->value(), 1.0);
    EXPECT_FALSE(ui.normalizeFlatsCheck->isChecked());
    EXPECT_TRUE(ui.preserveOriginalNameCheck->isChecked());
    EXPECT_EQ(ui.filenameSuffixEdit->text(), QStringLiteral("_cal"));
    EXPECT_EQ(ui.outputFormatSelector->currentData().toString(), QStringLiteral("fits"));
    EXPECT_EQ(ui.outputPrecisionSelector->currentData().toBool(), true);
    EXPECT_DOUBLE_EQ(ui.pedestalValue->value(), 0.0);
    EXPECT_FALSE(ui.darkAdvancedSection->isExpanded());
    EXPECT_FALSE(ui.flatAdvancedSection->isExpanded());
    EXPECT_FALSE(ui.outputAdvancedSection->isExpanded());

    EXPECT_EQ(ui.calibrateImagesButton->property("anButtonVariant").toString(), QStringLiteral("primary"));
    EXPECT_EQ(ui.clearAll->property("anButtonVariant").toString(), QStringLiteral("danger"));
    EXPECT_EQ(ui.addFiles->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.outputDirectoryButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.biasFileButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.darkFileButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.flatFileButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));

    EXPECT_EQ(ui.calibrateImagesButton->minimumHeight(), 44);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// The dialog can be made and let go again and again, each time whole.
TEST(ImageCalibrationProgrammaticUiTest, UiLifecycleRepeatedCreateDestroy)
{
    for (int i = 0; i < 3; ++i)
    {
        auto host = std::make_unique<QWidget>();
        auto ui = an::ui::processes::buildImageCalibrationProcessUi(host.get());
        ASSERT_NE(ui.tableWidget, nullptr);
        ASSERT_NE(ui.buttonBox, nullptr);
    }
}

// Every output format the dialog offers carries the text a run reads it by, and reads back as itself; the precision
// applies to FITS and TIFF and not to the light's own format.
TEST(ImageCalibrationProgrammaticUiTest, EveryOutputFormatTheDialogOffersIsOneARunReads)
{
    using an::ui::processes::CalibrationOutputFormat;
    QWidget host;
    auto ui = an::ui::processes::buildImageCalibrationProcessUi(&host);
    ASSERT_NE(ui.outputFormatSelector, nullptr);
    ASSERT_EQ(ui.outputFormatSelector->count(), 3);
    for (int index = 0; index < ui.outputFormatSelector->count(); ++index)
    {
        const QString text = ui.outputFormatSelector->itemData(index).toString();
        SCOPED_TRACE(text.toStdString());
        const CalibrationOutputFormat format = an::ui::processes::calibrationOutputFormatFromText(text);
        EXPECT_EQ(an::ui::processes::calibrationOutputFormatText(format), text);
        EXPECT_EQ(an::ui::processes::calibrationOutputPrecisionApplies(format),
                  format != CalibrationOutputFormat::Original);
    }
    EXPECT_EQ(an::ui::processes::calibrationOutputFormatFromText(QStringLiteral("original")),
              CalibrationOutputFormat::Original);
}

// The output controls open at the options a run starts from: the light's own name with "_cal" after it, written as
// Float32 FITS.
TEST(ImageCalibrationProgrammaticUiTest, TheOutputControlsOpenAtTheOptionsARunStartsFrom)
{
    const an::ui::processes::CalibrationOutputOptions defaults;
    QWidget host;
    auto ui = an::ui::processes::buildImageCalibrationProcessUi(&host);
    EXPECT_EQ(ui.preserveOriginalNameCheck->isChecked(), defaults.preserveOriginalName);
    EXPECT_EQ(ui.filenameSuffixEdit->text(), defaults.filenameSuffix);
    EXPECT_EQ(an::ui::processes::calibrationOutputFormatFromText(ui.outputFormatSelector->currentData().toString()),
              defaults.format);
    EXPECT_EQ(ui.outputPrecisionSelector->currentData().toBool(), defaults.use32BitFloat);
}

} // anonymous namespace
