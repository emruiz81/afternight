// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// F1, and Help in the top bar, from a task: the user guide opens at the page of the process the person is looking at,
/// on a task's page of every surface - a process with its preview, one whose canvas is its target, a process shown
/// whole - wherever the keyboard focus stands on that page: in the process's controls, on the task's header, nowhere
/// in particular. Away from a task, Help opens the guide's home page. What Help opens is what `helpPathFor` reads from
/// the widget it is asked from - the focus, or the active window - which is what the Help command reads.

#include "ObservatoryPages.h"

#include "controls/TopLevelProcessBaseControl.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace an::tests::observatory
{
namespace
{

/// The page Help opens from where the person is: the widget with the keyboard focus, or the active window.
[[nodiscard]] QString pageHelpOpens()
{
    const QWidget* const asked =
        QApplication::focusWidget() != nullptr ? QApplication::focusWidget() : QApplication::activeWindow();
    return an::ui::helpPathFor(asked);
}

class TaskHelp : public PagesCase
{
protected:
    /// Help from three places of a task's page: a control of the process, the task's header, and the window itself
    /// with no control focused. Each opens the process's page.
    static void expectTheProcessesPageFromEveryPlace(const Page page)
    {
        QWidget* const shown = open(page);
        ASSERT_NE(shown, nullptr) << pageName(page);
        TopLevelProcessBaseControl* const process = shownProcess();
        ASSERT_NE(process, nullptr) << pageName(page);
        const QString expected = process->helpPath();
        ASSERT_FALSE(expected.isEmpty()) << pageName(page) << " states no page of the guide";

        // The focus in one of the process's own controls.
        for (QWidget* const control : process->findChildren<QWidget*>())
        {
            if (control->isVisible() && control->isEnabled() && (control->focusPolicy() & Qt::TabFocus) != 0)
            {
                control->setFocus(Qt::OtherFocusReason);
                break;
            }
        }
        QCoreApplication::processEvents();
        EXPECT_EQ(pageHelpOpens().toStdString(), expected.toStdString())
            << pageName(page) << ": with the focus in the process's controls";

        // The focus on the task's header, outside the process.
        QWidget* const close = shown->findChild<QWidget*>(QStringLiteral("taskHeaderClose"));
        ASSERT_NE(close, nullptr) << pageName(page) << " has no header with a Close button";
        close->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        EXPECT_EQ(pageHelpOpens().toStdString(), expected.toStdString())
            << pageName(page) << ": with the focus on the task's header";

        // No control focused: Help is asked from the window.
        if (QWidget* const focused = QApplication::focusWidget(); focused != nullptr)
        {
            focused->clearFocus();
        }
        QCoreApplication::processEvents();
        EXPECT_EQ(an::ui::helpPathFor(&window()).toStdString(), expected.toStdString())
            << pageName(page) << ": asked from the window";
    }
};

} // namespace

// A process with its preview: Curves Transformation's page.
TEST_F(TaskHelp, APreviewTaskOpensItsProcesssPage)
{
    expectTheProcessesPageFromEveryPlace(Page::PreviewTask);
}

// A process whose canvas is its target: Resize's page.
TEST_F(TaskHelp, AMirrorTaskOpensItsProcesssPage)
{
    expectTheProcessesPageFromEveryPlace(Page::MirrorTask);
}

// A process shown whole: Image Integration's page.
TEST_F(TaskHelp, AFullPageOpensItsProcesssPage)
{
    expectTheProcessesPageFromEveryPlace(Page::ImageIntegration);
}

// Away from a task - on Home, on a workspace - Help opens the guide's home page, and it does again once a task closed.
TEST_F(TaskHelp, AwayFromATaskHelpOpensTheHomePage)
{
    (void)open(Page::Home);
    EXPECT_TRUE(an::ui::helpPathFor(&window()).isEmpty()) << "Home";
    leave();
    (void)open(Page::Workspace);
    EXPECT_TRUE(an::ui::helpPathFor(&window()).isEmpty()) << "a workspace";
    leave();
    (void)open(Page::PreviewTask);
    EXPECT_FALSE(an::ui::helpPathFor(&window()).isEmpty()) << "Curves Transformation's page";
    leave();
    EXPECT_TRUE(an::ui::helpPathFor(&window()).isEmpty()) << "Home, once the task closed";
}

} // namespace an::tests::observatory
