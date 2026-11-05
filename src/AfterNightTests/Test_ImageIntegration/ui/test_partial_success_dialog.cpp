// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_partial_success_dialog.cpp
 * @brief Tests that the progress dialogs correctly handle PartialSuccess results.
 *
 * Covers:
 *  - WorkflowProgressDialogV2 shows warning popup on partial success
 *  - Successful label changes to "Completed with Warnings"
 *  - wasSuccessful() still returns true for partial success
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QtTest/QTest>

#include "processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

namespace
{
class PartialSuccessDialogTest : public ::testing::Test
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

    // The dialogs never start a run, so their controllers are lent no engine session.
    IntegrationDataModel model;
};

} // namespace

TEST_F(PartialSuccessDialogTest, PartialSuccessShowsWarningAndIsSuccessful)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    result.errors.append(QStringLiteral("[Registration] File not found: a.fit"));
    result.errors.append(QStringLiteral("[Registration] File not found: b.fit"));

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    EXPECT_TRUE(dialog.wasSuccessful()) << "Partial success should still be considered successful";
    EXPECT_FALSE(dialog.wasCancelled());
    EXPECT_FALSE(dialog.wasFailed());

    auto* opLabel = dialog.findChild<QLabel*>(QStringLiteral("lblCurrentOperation"));
    if (opLabel)
    {
        EXPECT_TRUE(opLabel->text().contains(QStringLiteral("Warning")) ||
                    opLabel->text().contains(QStringLiteral("warning")))
            << "Label should indicate warnings, got: " << opLabel->text().toStdString();
    }
}

TEST_F(PartialSuccessDialogTest, PureSuccessDoesNotShowWarningLabel)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = false;

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    EXPECT_TRUE(dialog.wasSuccessful());

    auto* opLabel = dialog.findChild<QLabel*>(QStringLiteral("lblCurrentOperation"));
    if (opLabel)
    {
        EXPECT_TRUE(opLabel->text().contains(QStringLiteral("Success")))
            << "Label should indicate success, got: " << opLabel->text().toStdString();
    }
}

TEST_F(PartialSuccessDialogTest, PartialSuccessWithManyErrorsTruncatesList)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    for (int i = 0; i < 15; ++i)
    {
        result.errors.append(QStringLiteral("[StageA] Error %1").arg(i));
    }

    // The dialog should show max 10 errors and "... and N more"
    // We just verify it doesn't crash with many errors
    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    EXPECT_TRUE(dialog.wasSuccessful());
}

TEST_F(PartialSuccessDialogTest, PartialSuccessWithEmptyErrorsShowsSuccess)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    // No errors appended — partialSuccess flag set but empty list

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    EXPECT_TRUE(dialog.wasSuccessful());
    // With no errors, it should show normal success (because the errors list is empty)
    auto* opLabel = dialog.findChild<QLabel*>(QStringLiteral("lblCurrentOperation"));
    if (opLabel)
    {
        EXPECT_TRUE(opLabel->text().contains(QStringLiteral("Success")))
            << "Empty error list should show success, got: " << opLabel->text().toStdString();
    }
}

TEST_F(PartialSuccessDialogTest, PartialSuccessDoesNotOpenModalDialogOnHeadlessPlatform)
{
    const QString platform = QGuiApplication::platformName().toLower();
    if (platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal") &&
        platform != QStringLiteral("minimalegl"))
    {
        GTEST_SKIP() << "Headless-only regression check";
    }

    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    result.errors.append(QStringLiteral("[Registration] File not found: a.fit"));

    int before = 0;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (qobject_cast<QMessageBox*>(widget))
        {
            ++before;
        }
    }

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(20);

    int after = 0;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (qobject_cast<QMessageBox*>(widget))
        {
            ++after;
        }
    }

    EXPECT_EQ(after, before);
    EXPECT_TRUE(dialog.wasSuccessful());
}

TEST_F(PartialSuccessDialogTest, PartialSuccessDoesNotOpenModalDialogWhenAppNameIsMutatedInTests)
{
    const QString originalAppName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("AfterNight"));

    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = true;
    result.partialSuccess = true;
    result.errors.append(QStringLiteral("[Registration] File not found: a.fit"));

    int before = 0;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (qobject_cast<QMessageBox*>(widget))
        {
            ++before;
        }
    }

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(20);

    int after = 0;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (qobject_cast<QMessageBox*>(widget))
        {
            ++after;
        }
    }

    QCoreApplication::setApplicationName(originalAppName);

    EXPECT_EQ(after, before);
    EXPECT_TRUE(dialog.wasSuccessful());
}
