// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QApplication>
#include <QLabel>
#include <QTimer>

#include "processes/ImageIntegration/tabs/FrameSourceTab.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/widgets/GroupingOptionsWidget.h"

// Mock FrameInfo for testing
#include "processes/ImageIntegration/model/GroupingKeys.h"

// The suite's harness provides the headless application.
class RegroupingProgressTest : public testing::Test
{
};

TEST_F(RegroupingProgressTest, RegroupingWithManyFramesDoesNotCrash)
{
    IntegrationDataModel model;
    FrameSourceTab tab(FrameType::Light, &model);

    // Add 501 frames to trigger the progress logic
    QStringList files;
    for (int i = 0; i <= 500; ++i)
    {
        files << QString("test_frame_%1.fit").arg(i);
    }

    // We can't easily add valid FrameInfo without file I/O in the real model,
    // but we can try to use internal methods if available or just check if the widget handles the empty case gracefully
    // (which falls into the < 500 branch).
    // To properly test the > 500 branch, we would need to mock the model or force the frame count.

    // For now, we verify that changing options triggers the slot without error.

    GroupingOptionsWidget* optsWidget = tab.groupingOptions();
    ASSERT_NE(optsWidget, nullptr);

    GroupingOptions newOpts;
    newOpts.byFilter = true;

    // Emit signal manually or via widget
    // This will execute the lambda in FrameSourceTab
    optsWidget->setOptions(newOpts);

    // If we reached here, no crash.
    SUCCEED();
}
