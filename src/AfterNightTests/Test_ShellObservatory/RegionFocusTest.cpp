// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// `F6` and `Shift+F6` in the Observatory window, as a person who works from the keyboard meets them: the focus moves
/// through the rail, the centre and the column in that order and back, passes over a region that is not shown, finds a
/// task page's column inside the centre, and comes back to the control it left a region from; the keys are the shell's
/// Next Region and Previous Region, in the View menu and the palette, and the focus they give is the keyboard's.

#include "ObservatoryWindowCase.h"

#include "ObservatoryWindow.h"
#include "chrome/MenuBarOnAlt.h"
#include "chrome/PaletteField.h"
#include "chrome/TopBar.h"
#include "pages/TaskPage.h"
#include "tasks/TaskPresenter.h"

#include "actions/ActionCatalogue.h"
#include "commands/CommandIndex.h"
#include "components/AnIconRail.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationCore.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QMenu>
#include <QStackedWidget>
#include <QWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{
namespace
{

constexpr std::string_view kCurves = "transforms.actionCurves_Transformation";
constexpr char kNextRegion[] = "actionFocus_Next_Region";
constexpr char kPreviousRegion[] = "actionFocus_Previous_Region";

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::processEvents();
    }
}

class RegionFocus : public ObservatoryWindowCase
{
protected:
    void SetUp() override
    {
        ObservatoryWindowCase::SetUp();
        if (!an::tests::app::activateWindow(window()))
        {
            GTEST_SKIP() << "the platform gives the window no activation, so no widget of it has the focus";
        }
    }

    void TearDown() override
    {
        for (const an::ui::shell::Task& task : std::vector<an::ui::shell::Task>(tasks().model().tasks()))
        {
            if (!task.process.isNull())
            {
                (void)task.process->close();
            }
        }
        settle();
        ObservatoryWindowCase::TearDown();
    }

    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* const presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    /// Opens Curves Transformation over the active view, as its menu entry does: its page is shown.
    static TopLevelProcessBaseControl* openCurves()
    {
        EXPECT_TRUE(core().processLaunch().launch(std::string(kCurves)));
        settle();
        const std::vector<an::ui::shell::Task>& now = tasks().model().tasks();
        return now.empty() ? nullptr : now.back().process.data();
    }

    /// The column of the task page shown; null on any other place.
    [[nodiscard]] static QWidget* taskColumn()
    {
        const an::ui::observatory::TaskPage* const page =
            window().isTaskPageShown() ? window().taskPage(window().shownTask()) : nullptr;
        const TopLevelProcessBaseControl* const process = page != nullptr ? page->process() : nullptr;
        return process != nullptr && process->pageLayout() != nullptr ? process->pageLayout()->column() : nullptr;
    }

    /// The region the focus is in, as a person sees the window: the rail, the column - a task page's, or the readout's
    /// - the centre, elsewhere in the window, or none.
    [[nodiscard]] static std::string regionOfTheFocus()
    {
        const QWidget* const focus = QApplication::focusWidget();
        const auto liesIn = [focus](const QWidget* area) {
            return area != nullptr && (focus == area || area->isAncestorOf(focus));
        };
        if (focus == nullptr)
        {
            return "none";
        }
        if (liesIn(window().rail()))
        {
            return "rail";
        }
        if (liesIn(taskColumn()) || (window().rightColumn()->isVisible() && liesIn(window().rightColumn())))
        {
            return "column";
        }
        if (liesIn(window().centre()->currentWidget()))
        {
            return "centre";
        }
        return liesIn(&window()) ? "elsewhere" : "outside";
    }

    /// Presses a key where the focus is, as the keyboard does.
    static void press(const Qt::Key key, const Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QWidget* const focus = QApplication::focusWidget();
        QTest::keyClick(focus != nullptr ? focus : &window(), key, modifiers);
        settle();
    }

    /// The regions the focus is in after each of `presses` presses of a key, from where it is now.
    [[nodiscard]] static std::vector<std::string> walk(const Qt::Key key, const Qt::KeyboardModifiers modifiers,
                                                       const int presses)
    {
        std::vector<std::string> regions;
        for (int count = 0; count < presses; ++count)
        {
            press(key, modifiers);
            regions.push_back(regionOfTheFocus());
        }
        return regions;
    }

    /// The focus put in the top bar, outside every region.
    static void focusTheTopBar()
    {
        window().topBar()->paletteField()->setFocus(Qt::MouseFocusReason);
        settle();
        ASSERT_EQ(regionOfTheFocus(), "elsewhere");
    }
};

// On a workspace with an image open, F6 moves the focus from the top bar into the rail, then the centre - the active
// image - then the column, and back to the rail; Shift+F6 goes the other way round.
TEST_F(RegionFocus, F6AndShiftF6MoveThroughTheRegionsInOrderAndBack)
{
    ImageViewControl* const view = openView(QStringLiteral("regions"));
    ASSERT_NE(view, nullptr);
    window().showViews();
    settle();
    ASSERT_TRUE(window().isWorkspaceShown());
    ASSERT_TRUE(window().rightColumn()->isVisible());
    focusTheTopBar();

    EXPECT_EQ(walk(Qt::Key_F6, Qt::NoModifier, 4), (std::vector<std::string>{"rail", "centre", "column", "rail"}));
    EXPECT_EQ(walk(Qt::Key_F6, Qt::ShiftModifier, 4), (std::vector<std::string>{"column", "centre", "rail", "column"}));

    // The centre's focus is the active image's.
    press(Qt::Key_F6, Qt::ShiftModifier);
    ASSERT_EQ(regionOfTheFocus(), "centre");
    EXPECT_TRUE(view->isAncestorOf(QApplication::focusWidget()))
        << "the focus is on " << QApplication::focusWidget()->metaObject()->className() << ", not the active image";
}

// On Home there is no column: F6 goes from the rail to Home and back to the rail.
TEST_F(RegionFocus, ARegionThatIsNotShownIsPassedOver)
{
    window().showHome();
    settle();
    ASSERT_TRUE(window().isHomeShown());
    ASSERT_FALSE(window().rightColumn()->isVisible());
    focusTheTopBar();

    EXPECT_EQ(walk(Qt::Key_F6, Qt::NoModifier, 3), (std::vector<std::string>{"rail", "centre", "rail"}));
    EXPECT_EQ(walk(Qt::Key_F6, Qt::ShiftModifier, 2), (std::vector<std::string>{"centre", "rail"}));
}

// On a task's page the process's column is the column, inside the centre: F6 goes from the rail to the canvas - the
// preview - then into the process's column, and back to the rail.
TEST_F(RegionFocus, OnATaskPageTheProcessColumnIsTheColumn)
{
    ASSERT_NE(openView(QStringLiteral("regions-task")), nullptr);
    TopLevelProcessBaseControl* const process = openCurves();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(window().isTaskPageShown());
    ASSERT_NE(taskColumn(), nullptr);
    focusTheTopBar();

    EXPECT_EQ(walk(Qt::Key_F6, Qt::NoModifier, 4), (std::vector<std::string>{"rail", "centre", "column", "rail"}));
    press(Qt::Key_F6, Qt::NoModifier);
    ASSERT_EQ(regionOfTheFocus(), "centre");
    const ImageViewControl* const preview = process->pageLayout()->previewView();
    ASSERT_NE(preview, nullptr);
    EXPECT_TRUE(preview->isAncestorOf(QApplication::focusWidget()))
        << "the canvas's focus is on " << QApplication::focusWidget()->metaObject()->className();
}

// The focus comes back to the control it left a region from: a control deep in a task's column, left for the rail
// with F6, is where Shift+F6 brings the focus back.
TEST_F(RegionFocus, TheFocusComesBackToTheControlItLeft)
{
    ASSERT_NE(openView(QStringLiteral("regions-back")), nullptr);
    ASSERT_NE(openCurves(), nullptr);
    QWidget* const column = taskColumn();
    ASSERT_NE(column, nullptr);
    // The column's last control that takes the focus from the keyboard.
    QWidget* deep = nullptr;
    for (QWidget* widget : column->findChildren<QWidget*>())
    {
        if (widget->isVisibleTo(&window()) && widget->isEnabled() && (widget->focusPolicy() & Qt::TabFocus) != 0)
        {
            deep = widget;
        }
    }
    ASSERT_NE(deep, nullptr);
    deep->setFocus(Qt::MouseFocusReason);
    settle();
    ASSERT_EQ(QApplication::focusWidget(), deep);

    press(Qt::Key_F6, Qt::NoModifier);
    ASSERT_EQ(regionOfTheFocus(), "rail");
    press(Qt::Key_F6, Qt::ShiftModifier);
    EXPECT_EQ(QApplication::focusWidget(), deep)
        << "the focus went to " << QApplication::focusWidget()->metaObject()->className();
}

// The keys are the shell's own actions, Next Region and Previous Region, in the View menu and the palette; the focus
// they give is the keyboard's, so its ring is drawn.
TEST_F(RegionFocus, TheKeysAreTheShellsNextRegionAndPreviousRegion)
{
    QAction* next = nullptr;
    QAction* previous = nullptr;
    for (QAction* action : window().findChildren<QAction*>())
    {
        next = action->objectName() == QLatin1String(kNextRegion) ? action : next;
        previous = action->objectName() == QLatin1String(kPreviousRegion) ? action : previous;
    }
    ASSERT_NE(next, nullptr);
    ASSERT_NE(previous, nullptr);
    EXPECT_EQ(next->text(), QStringLiteral("Next Region"));
    EXPECT_EQ(previous->text(), QStringLiteral("Previous Region"));
    EXPECT_EQ(next->shortcut(), QKeySequence(Qt::Key_F6));
    EXPECT_EQ(previous->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F6));
    EXPECT_EQ(core().actions().action(kNextRegion), nullptr) << "it is in the catalogue's base table";

    QMenu* const view = window().menuBarOnAlt().menu("menuView_2");
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->actions().contains(next));
    EXPECT_TRUE(view->actions().contains(previous));

    core().commands().refresh();
    QStringList offered;
    for (const an::ui::shell::CommandEntry& entry : core().commands().entries())
    {
        offered.append(entry.id);
    }
    EXPECT_TRUE(offered.contains(QStringLiteral("action:") + QLatin1String(kNextRegion)));
    EXPECT_TRUE(offered.contains(QStringLiteral("action:") + QLatin1String(kPreviousRegion)));

    focusTheTopBar();
    window().setAttribute(Qt::WA_KeyboardFocusChange, false);
    press(Qt::Key_F6, Qt::NoModifier);
    ASSERT_EQ(regionOfTheFocus(), "rail");
    EXPECT_TRUE(window().testAttribute(Qt::WA_KeyboardFocusChange)) << "the focus F6 gives is not the keyboard's";
}

} // namespace
} // namespace an::tests::observatory
