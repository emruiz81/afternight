// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QCheckBox>

#include "processes/ImageIntegration/widgets/GroupingOptionsWidget.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"
#include "processes/ImageIntegration/model/FrameType.h"

class GroupingOptionsWidgetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        widget = new GroupingOptionsWidget();
    }

    void TearDown() override
    {
        delete widget;
    }

    GroupingOptionsWidget* widget = nullptr;
};

TEST_F(GroupingOptionsWidgetTest, InitialStateReflectsDefaultOptions)
{
    GroupingOptions defaults; // Default constructor sets defaults
    GroupingOptions current = widget->currentOptions();
    EXPECT_EQ(current, defaults);
}

TEST_F(GroupingOptionsWidgetTest, SetOptionsUpdatesUi)
{
    GroupingOptions opts;
    opts.byFilter = true;
    opts.byObject = false;
    opts.bySession = true;
    opts.byIsoGain = true;
    opts.byExposure = true;
    opts.byCustomTag = true;

    widget->setOptions(opts);
    GroupingOptions actual = widget->currentOptions();

    // Resolution grouping is mandatory and not user-configurable.
    EXPECT_TRUE(actual.byResolution);
    EXPECT_EQ(actual.byFilter, opts.byFilter);
    EXPECT_EQ(actual.byObject, opts.byObject);
    EXPECT_EQ(actual.bySession, opts.bySession);
    EXPECT_EQ(actual.byIsoGain, opts.byIsoGain);
    EXPECT_EQ(actual.byExposure, opts.byExposure);
    EXPECT_EQ(actual.byCustomTag, opts.byCustomTag);

    // Verify specific widgets (assuming access via findChild or public accessor if we add one,
    // or just relying on currentOptions() effectively testing the UI state synchronization)
    // For a unit test of a widget, testing the public API (setOptions/currentOptions) is usually sufficient
    // if the implementation binds them.
}

TEST_F(GroupingOptionsWidgetTest, InteractionEmitsSignal)
{
    QSignalSpy spy(widget, &GroupingOptionsWidget::optionsChanged);

    // Toggle a checkbox programmatically works like a click if we find it,
    // or we can assume the widget exposes specific checkboxes or we simulate click?
    // Since I'm implementing the widget, I can verify the signal is emitted when setOptions is called?
    // No, setOptions usually shouldn't emit if it's programmatic.
    // User interaction should emit.

    // Let's find a checkbox. I'll name them predictably in implementation.
    QCheckBox* chkFilter = widget->findChild<QCheckBox*>("chkFilter");
    ASSERT_TRUE(chkFilter != nullptr);

    // Simulate click
    chkFilter->setChecked(!chkFilter->isChecked());

    EXPECT_EQ(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    GroupingOptions emittedOpts = args.at(0).value<GroupingOptions>();
    EXPECT_NE(emittedOpts.byFilter, GroupingOptions().byFilter); // Should be flipped
}

TEST_F(GroupingOptionsWidgetTest, FrameTypeHidesIrrelevantOptions)
{
    QCheckBox* chkResolution = widget->findChild<QCheckBox*>("chkResolution");
    QCheckBox* chkFilter = widget->findChild<QCheckBox*>("chkFilter");
    QCheckBox* chkObject = widget->findChild<QCheckBox*>("chkObject");
    QCheckBox* chkSession = widget->findChild<QCheckBox*>("chkSession");
    QCheckBox* chkIsoGain = widget->findChild<QCheckBox*>("chkIsoGain");
    QCheckBox* chkExposure = widget->findChild<QCheckBox*>("chkExposure");
    QCheckBox* chkCustomTag = widget->findChild<QCheckBox*>("chkCustomTag");

    // Resolution grouping is mandatory and is not exposed as a checkbox.
    EXPECT_TRUE(chkResolution == nullptr);
    ASSERT_TRUE(chkFilter != nullptr);
    ASSERT_TRUE(chkObject != nullptr);
    ASSERT_TRUE(chkSession != nullptr);
    ASSERT_TRUE(chkIsoGain != nullptr);
    ASSERT_TRUE(chkExposure != nullptr);
    ASSERT_TRUE(chkCustomTag != nullptr);

    // Bias: Resolution, IsoGain, CustomTag visible; Filter, Object, Session, Exposure hidden
    widget->setFrameType(FrameType::Bias);
    EXPECT_TRUE(chkFilter->isHidden());
    EXPECT_TRUE(chkObject->isHidden());
    EXPECT_TRUE(chkSession->isHidden());
    EXPECT_FALSE(chkIsoGain->isHidden());
    EXPECT_TRUE(chkExposure->isHidden());
    EXPECT_FALSE(chkCustomTag->isHidden());

    // Dark: Resolution, IsoGain, Exposure, CustomTag visible; Filter, Object, Session hidden
    widget->setFrameType(FrameType::Dark);
    EXPECT_TRUE(chkFilter->isHidden());
    EXPECT_TRUE(chkObject->isHidden());
    EXPECT_TRUE(chkSession->isHidden());
    EXPECT_FALSE(chkIsoGain->isHidden());
    EXPECT_FALSE(chkExposure->isHidden());
    EXPECT_FALSE(chkCustomTag->isHidden());

    // Flat: All except Exposure visible
    widget->setFrameType(FrameType::Flat);
    EXPECT_FALSE(chkFilter->isHidden());
    EXPECT_FALSE(chkObject->isHidden());
    EXPECT_FALSE(chkSession->isHidden());
    EXPECT_FALSE(chkIsoGain->isHidden());
    EXPECT_TRUE(chkExposure->isHidden());
    EXPECT_FALSE(chkCustomTag->isHidden());

    // Light: All visible
    widget->setFrameType(FrameType::Light);
    EXPECT_FALSE(chkFilter->isHidden());
    EXPECT_FALSE(chkObject->isHidden());
    EXPECT_FALSE(chkSession->isHidden());
    EXPECT_FALSE(chkIsoGain->isHidden());
    EXPECT_FALSE(chkExposure->isHidden());
    EXPECT_FALSE(chkCustomTag->isHidden());
}

TEST_F(GroupingOptionsWidgetTest, DefaultToleranceValuesPersistWhenHidden)
{
    GroupingOptions defaults;

    widget->setFrameType(FrameType::Flat);
    GroupingOptions current = widget->currentOptions();

    EXPECT_EQ(current.exposureToleranceSeconds, defaults.exposureToleranceSeconds);
    EXPECT_EQ(current.temperatureToleranceC, defaults.temperatureToleranceC);
}
