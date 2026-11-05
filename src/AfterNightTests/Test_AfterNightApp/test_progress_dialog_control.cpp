// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// ProgressDialogControl modernization tests.
#include <gtest/gtest.h>

#include <QDialog>
#include <QProgressBar>
#include <QPushButton>

#include "controls/ProgressDialogControlUiBuilder.h"
#include "components/AnProgressCard.h"

namespace
{

class ProgressDialogUiTest : public ::testing::Test
{
protected:
    QDialog parent;
};

} // namespace

TEST_F(ProgressDialogUiTest, BuilderUsesAnProgressCard)
{
    // The modernized builder must produce a primary AnProgressCard, not a raw QProgressBar.
    auto ui = an::ui::controls::buildProgressDialogControlUi(&parent);

    ASSERT_NE(ui.progressCard, nullptr) << "Builder must populate progressCard with an AnProgressCard";
    ASSERT_NE(ui.cancelButton, nullptr);
}

TEST_F(ProgressDialogUiTest, SetProgressValueUpdatesCard)
{
    auto ui = an::ui::controls::buildProgressDialogControlUi(&parent);
    ASSERT_NE(ui.progressCard, nullptr);

    ui.progressCard->setProgressValue(42);

    EXPECT_EQ(ui.progressCard->progressValue(), 42);
}

TEST_F(ProgressDialogUiTest, SequenceProgressCardExists)
{
    auto ui = an::ui::controls::buildProgressDialogControlUi(&parent);

    ASSERT_NE(ui.sequenceProgressCard, nullptr) << "Builder must produce a sequenceProgressCard for two-step mode";
}

TEST_F(ProgressDialogUiTest, CancelButtonHasObjectName)
{
    auto ui = an::ui::controls::buildProgressDialogControlUi(&parent);
    ASSERT_NE(ui.cancelButton, nullptr);

    EXPECT_EQ(ui.cancelButton->objectName(), QStringLiteral("cancelButton"));
}

TEST_F(ProgressDialogUiTest, SetProgressRangeForwards)
{
    auto ui = an::ui::controls::buildProgressDialogControlUi(&parent);
    ASSERT_NE(ui.progressCard, nullptr);

    ui.progressCard->setProgressRange(0, 200);

    EXPECT_EQ(ui.progressCard->progressMaximum(), 200);
}
