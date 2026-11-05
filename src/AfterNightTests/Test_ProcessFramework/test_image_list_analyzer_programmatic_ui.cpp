// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image List Analyzer dialog as its builder makes it: its preview, histogram, table and summary at their sizes,
/// its buttons, and the dialog made and let go repeatedly.

#include <gtest/gtest.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <memory>

#include "processes/ImageListAnalyzerProcessUiBuilder.h"

namespace
{

// The dialog opens with its preview, histogram and table at their minimum sizes, a read-only summary, Optimize For Fast
// Play and Export Sequence, and Close and Reset.
TEST(ImageListAnalyzerProgrammaticUiTest, ConstructorDefaultsMatchContract)
{
    QWidget host;
    auto ui = an::ui::processes::buildImageListAnalyzerProcessUi(&host);

    ASSERT_NE(ui.verticalLayout_Main, nullptr);
    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.tableLayout, nullptr);
    ASSERT_NE(ui.imagePreviewWidget, nullptr);
    ASSERT_NE(ui.imageSummary, nullptr);
    ASSERT_NE(ui.histogramWidget, nullptr);
    ASSERT_NE(ui.optimizeButton, nullptr);
    ASSERT_NE(ui.showInfoLabel, nullptr);
    ASSERT_NE(ui.exportSequenceButton, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.addFiles, nullptr);
    ASSERT_NE(ui.removeSelected, nullptr);
    ASSERT_NE(ui.currentImageLabel, nullptr);
    ASSERT_NE(ui.totalImagesLabel, nullptr);
    ASSERT_NE(ui.clearAll, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.windowTitle(), QStringLiteral("Image List Analyzer"));
    EXPECT_EQ(host.minimumSize(), QSize(1200, 900));
    EXPECT_EQ(ui.imagePreviewWidget->minimumSize(), QSize(500, 400));
    EXPECT_EQ(ui.histogramWidget->minimumHeight(), 250);
    EXPECT_EQ(ui.tableWidget->minimumHeight(), 250);
    EXPECT_TRUE(ui.imageSummary->isReadOnly());
    EXPECT_EQ(ui.showInfoLabel->alignment(), Qt::AlignCenter);
    EXPECT_EQ(ui.exportSequenceButton->minimumHeight(), 50);
    EXPECT_EQ(ui.optimizeButton->text(), QStringLiteral("Optimize For Fast Play"));
    EXPECT_TRUE(ui.showInfoLabel->text().isEmpty());

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
}

// The dialog can be made and let go again and again, each time whole.
TEST(ImageListAnalyzerProgrammaticUiTest, UiLifecycleRepeatedCreateDestroy)
{
    for (int i = 0; i < 3; ++i)
    {
        auto host = std::make_unique<QWidget>();
        auto ui = an::ui::processes::buildImageListAnalyzerProcessUi(host.get());
        ASSERT_NE(ui.tableWidget, nullptr);
        ASSERT_NE(ui.buttonBox, nullptr);
    }
}

} // anonymous namespace
