// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QApplication>
#include <QPushButton>
#include <QToolButton>
#include <QSlider>

#include "controls/ImageViewControl.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "widgets/ComparisonViewWidget.h"

// Access private members for testing
class TestBlinkComparatorDialog : public BlinkComparatorDialog
{
public:
    using BlinkComparatorDialog::BlinkComparatorDialog;

    ImageViewControl* getLeftPanel() const
    {
        return leftPanel();
    }
    ImageViewControl* getRightPanel() const
    {
        return rightPanel();
    }
    an::ui::ComparisonViewWidget* getComparisonView() const
    {
        return comparisonView();
    }
};

class BlinkComparatorZoomTest : public testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance())
        {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            static QApplication app(argc, argv);
        }
    }
};

TEST_F(BlinkComparatorZoomTest, ZoomLockRemainsEnabledWithoutToolbarButton)
{
    QStringList files;
    TestBlinkComparatorDialog dialog(files);

    EXPECT_EQ(dialog.findChild<QToolButton*>(QStringLiteral("lockZoomButton")), nullptr);
    ASSERT_NE(dialog.getComparisonView(), nullptr);
    EXPECT_TRUE(dialog.getComparisonView()->isZoomLocked());
}

TEST_F(BlinkComparatorZoomTest, ZoomToolbarButtonsAreRemoved)
{
    QStringList files;
    TestBlinkComparatorDialog dialog(files);

    EXPECT_EQ(dialog.findChild<QPushButton*>(QStringLiteral("fitButton")), nullptr);
    EXPECT_EQ(dialog.findChild<QPushButton*>(QStringLiteral("oneToOneButton")), nullptr);
    EXPECT_EQ(dialog.findChild<QToolButton*>(QStringLiteral("lockZoomButton")), nullptr);
}

TEST_F(BlinkComparatorZoomTest, ZoomSliderIsRemovedFromToolbar)
{
    QStringList files;
    TestBlinkComparatorDialog dialog(files);
    auto* slider = dialog.findChild<QSlider*>("zoomSlider");
    EXPECT_EQ(slider, nullptr);
    ASSERT_NE(dialog.getComparisonView(), nullptr);
    EXPECT_TRUE(dialog.getComparisonView()->isZoomLocked());
}
