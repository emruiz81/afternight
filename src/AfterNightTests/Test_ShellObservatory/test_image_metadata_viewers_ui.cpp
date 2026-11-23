// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// Metadata viewer control modernization tests.
#include <gtest/gtest.h>

#include <QAbstractItemView>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QWidget>

#include "controls/ImageEXIFViewerControlUiBuilder.h"
#include "controls/ImageFITSHeaderViewerControlUiBuilder.h"
#include "controls/ImageStatisticsControlUiBuilder.h"
#include "controls/ImageSummaryViewerControlUiBuilder.h"

TEST(ImageMetadataViewerUiTest, ExifViewerBuilder_UsesPlainHeaderAndPrimaryCloseAction)
{
    QDialog host;
    auto ui = an::ui::controls::buildImageEXIFViewerControlUi(&host);

    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.closeButton, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(680, 420));
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageExifHeaderCard")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageExifMetadataCard")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription")), nullptr);

    EXPECT_EQ(ui.tableWidget->selectionBehavior(), QAbstractItemView::SelectRows);
    EXPECT_EQ(ui.closeButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_TRUE(ui.closeButton->isDefault());
    EXPECT_EQ(ui.closeButton->parentWidget(), &host);
}

TEST(ImageMetadataViewerUiTest, FitsViewerBuilder_UsesPlainHeaderAndPrimaryCloseAction)
{
    QDialog host;
    auto ui = an::ui::controls::buildImageFITSHeaderViewerControlUi(&host);

    ASSERT_NE(ui.verticalLayout_2, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.closeButton, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(680, 420));
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageFitsHeaderCard")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageFitsTableCard")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription")), nullptr);

    EXPECT_EQ(ui.tableWidget->selectionBehavior(), QAbstractItemView::SelectRows);
    EXPECT_EQ(ui.closeButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_TRUE(ui.closeButton->isDefault());
    EXPECT_EQ(ui.closeButton->parentWidget(), &host);
}

TEST(ImageMetadataViewerUiTest, SummaryViewerBuilder_UsesPlainHeaderAndStructuredContent)
{
    QDialog host;
    auto ui = an::ui::controls::buildImageSummaryViewerControlUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    host.setLayout(ui.verticalLayout);

    ASSERT_NE(ui.summaryScrollArea, nullptr);
    ASSERT_NE(ui.summaryContentWidget, nullptr);
    ASSERT_NE(ui.summaryContentLayout, nullptr);
    ASSERT_NE(ui.copyButton, nullptr);
    ASSERT_NE(ui.closeButton, nullptr);
    EXPECT_EQ(host.minimumSize(), QSize(680, 420));
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageSummaryHeaderCard")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageSummaryBodyCard")), nullptr);
    EXPECT_EQ(host.findChild<QTextEdit*>(QStringLiteral("textEdit")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription")), nullptr);
    EXPECT_EQ(ui.closeButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_TRUE(ui.closeButton->isDefault());
    EXPECT_EQ(ui.closeButton->parentWidget(), &host);
}

TEST(ImageMetadataViewerUiTest, StatisticsUnclippedCheckboxMapsToEngineClippingMode)
{
    EXPECT_TRUE(an::ui::controls::detail::statisticsShouldExcludeClippedSamples(false));
    EXPECT_FALSE(an::ui::controls::detail::statisticsShouldExcludeClippedSamples(true));
}
