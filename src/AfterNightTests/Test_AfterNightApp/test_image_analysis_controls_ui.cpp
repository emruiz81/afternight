// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// Histogram/statistics control modernization tests.
#include <gtest/gtest.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "components/AnStyledTable.h"
#include "controls/ImageHistogramControlUiBuilder.h"
#include "controls/ImageHistoryControlUiBuilder.h"
#include "controls/ImageStatisticsControlUiBuilder.h"

TEST(ImageAnalysisControlsUiTest, HistogramBuilder_UsesPlainHeaderAndModernToolbar)
{
    QDialog host;
    auto ui = an::ui::controls::buildImageHistogramControlUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.chartScrollArea, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(700, 460));
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageHistogramHeaderCard")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageHistogramBodyCard")), nullptr);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageHistogramChartCard")), nullptr);
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageHistogramControlsCard")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription")), nullptr);

    EXPECT_NE(ui.zoomIn, nullptr);
    EXPECT_NE(ui.zoomOut, nullptr);
    EXPECT_NE(ui.zoomReset, nullptr);
    EXPECT_NE(ui.gridButton, nullptr);
    EXPECT_NE(ui.showClippingInfo, nullptr);
    EXPECT_NE(ui.localChannelNormalization, nullptr);
    EXPECT_NE(ui.showHistogramAreas, nullptr);
    EXPECT_NE(ui.plotResolution, nullptr);

    auto* closeButton = ui.buttonBox->button(QDialogButtonBox::Close);
    ASSERT_NE(closeButton, nullptr);
    EXPECT_EQ(closeButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_EQ(ui.buttonBox->parentWidget(), &host);

    auto* bodyCard = host.findChild<QWidget*>(QStringLiteral("imageHistogramBodyCard"));
    ASSERT_NE(bodyCard, nullptr);
    EXPECT_EQ(bodyCard->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox")), nullptr);
}

TEST(ImageAnalysisControlsUiTest, StatisticsBuilder_UsesPlainHeaderAndStyledTable)
{
    QDialog host;
    auto ui = an::ui::controls::buildImageStatisticsControlUi(&host);

    ASSERT_NE(ui.mainLayout, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.comboBox, nullptr);
    ASSERT_NE(ui.refreshButton, nullptr);
    ASSERT_NE(ui.closeButton, nullptr);
    ASSERT_NE(ui.unclipped, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(680, 420));
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageStatisticsHeaderCard")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageStatisticsBodyCard")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription")), nullptr);

    auto* styledTable = dynamic_cast<an::ui::AnStyledTable*>(ui.tableWidget);
    EXPECT_NE(styledTable, nullptr);

    EXPECT_EQ(ui.closeButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_TRUE(ui.closeButton->isDefault());
    EXPECT_EQ(ui.closeButton->parentWidget(), &host);
}

TEST(ImageAnalysisControlsUiTest, HistoryBuilder_UsesPlainHeaderAndModernButtonVariants)
{
    QDialog host;
    auto ui = an::ui::controls::buildImageHistoryControlUi(&host);

    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.contentLayout, nullptr);
    ASSERT_NE(ui.tableWidget, nullptr);
    ASSERT_NE(ui.moveUpButton, nullptr);
    ASSERT_NE(ui.moveDownButton, nullptr);
    ASSERT_NE(ui.refreshButton, nullptr);
    ASSERT_NE(ui.closeButton, nullptr);
    ASSERT_NE(ui.previewCard, nullptr);
    ASSERT_NE(ui.selectedStepTitleLabel, nullptr);
    ASSERT_NE(ui.selectedStepSubtitleLabel, nullptr);
    ASSERT_NE(ui.previewWidgetHost, nullptr);
    ASSERT_NE(ui.previewWidgetHostLayout, nullptr);
    ASSERT_NE(ui.histogramWidgetHost, nullptr);
    ASSERT_NE(ui.histogramWidgetHostLayout, nullptr);

    EXPECT_EQ(host.minimumSize(), QSize(920, 580));
    EXPECT_EQ(host.findChild<QWidget*>(QStringLiteral("imageHistoryHeaderCard")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageHistoryBodyCard")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("imageHistoryPreviewCard")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderTitle")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("anProcessShellHeaderDescription")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("historyStepPreviewTitle")), nullptr);
    EXPECT_NE(host.findChild<QLabel*>(QStringLiteral("historyStepPreviewSubtitle")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("historyStepPreviewHost")), nullptr);
    EXPECT_NE(host.findChild<QWidget*>(QStringLiteral("historyStepHistogramHost")), nullptr);

    auto* styledTable = dynamic_cast<an::ui::AnStyledTable*>(ui.tableWidget);
    EXPECT_NE(styledTable, nullptr);

    EXPECT_EQ(ui.moveUpButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.moveDownButton->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.refreshButton->property("buttonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(ui.closeButton->property("buttonVariant").toString(), QStringLiteral("primary"));
    EXPECT_TRUE(ui.closeButton->isDefault());
    EXPECT_EQ(ui.closeButton->parentWidget(), &host);
}
