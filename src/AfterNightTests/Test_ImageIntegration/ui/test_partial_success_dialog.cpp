// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_partial_success_dialog.cpp
 * @brief How a run's view says a run that succeeded with some errors.
 *

 * * Covers:
 *  - the view says "Completed with Warnings" for a run that succeeded with errors
 *  - wasSuccessful()
 * still returns true for partial success
 *  - a run that succeeded with no errors is said as a success, however many
 * errors are kept
 *  - the end of a run opens no message box: the view says it in itself
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QtTest/QTest>

#include "processes/ImageIntegration/widgets/WorkflowRunView.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

namespace
{
class PartialSuccessRunViewTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (!QApplication::instance())
        {
            static int argc = 0;
            static char* argv[] = {nullptr};
            new QApplication(argc, argv);
        }
    }

    // The views never start a run, so their controllers are lent no engine session.
    IntegrationDataModel model;
};

[[nodiscard]] QString operationText(const WorkflowRunView& view)
{
    const auto* const label = view.findChild<QLabel*>(QStringLiteral("lblCurrentOperation"));
    return label != nullptr ? label->text() : QString();
}

[[nodiscard]] int openMessageBoxes()
{
    int open = 0;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (qobject_cast<QMessageBox*>(widget) != nullptr && widget->isVisible())
        {
            ++open;
        }
    }
    return open;
}

} // namespace

TEST_F(PartialSuccessRunViewTest, PartialSuccessShowsWarningAndIsSuccessful)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    result.errors.append(QStringLiteral("[Registration] File not found: a.fit"));
    result.errors.append(QStringLiteral("[Registration] File not found: b.fit"));

    view.onFinished(result);
    QCoreApplication::processEvents();

    EXPECT_TRUE(view.wasSuccessful()) << "Partial success should still be considered successful";
    EXPECT_FALSE(view.wasCancelled());
    EXPECT_FALSE(view.wasFailed());
    EXPECT_TRUE(operationText(view).contains(QStringLiteral("Warning"), Qt::CaseInsensitive))
        << "Label should indicate warnings, got: " << operationText(view).toStdString();
}

TEST_F(PartialSuccessRunViewTest, PureSuccessDoesNotShowWarningLabel)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = false;

    view.onFinished(result);
    QCoreApplication::processEvents();

    EXPECT_TRUE(view.wasSuccessful());
    EXPECT_TRUE(operationText(view).contains(QStringLiteral("Success")))
        << "Label should indicate success, got: " << operationText(view).toStdString();
}

TEST_F(PartialSuccessRunViewTest, PartialSuccessWithEmptyErrorsShowsSuccess)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    // No errors appended: the partial success flag is set, but the list is empty.

    view.onFinished(result);
    QCoreApplication::processEvents();

    EXPECT_TRUE(view.wasSuccessful());
    EXPECT_TRUE(operationText(view).contains(QStringLiteral("Success")))
        << "Empty error list should show success, got: " << operationText(view).toStdString();
}

// The view says how a run ended in itself, however many errors it kept: no message box opens over it.
TEST_F(PartialSuccessRunViewTest, PartialSuccessOpensNoMessageBox)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowRunView view(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    for (int i = 0; i < 15; ++i)
    {
        result.errors.append(QStringLiteral("[StageA] Error %1").arg(i));
    }

    const int before = openMessageBoxes();
    view.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(20);

    EXPECT_EQ(openMessageBoxes(), before);
    EXPECT_TRUE(view.wasSuccessful());
}
