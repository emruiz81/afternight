// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sessions of the extension host in the Observatory window, over a stand-in for the launch coordinator: a session
/// is a rail item from its start - its image's thumbnail, the extension's glyph, the tooltip that says it opens in its
/// own window - and goes when it ends; the arc follows the run its host tells of, and the run blocks no image; a click
/// and the menu's Show ask the host to raise its window, which only a host that raises it is asked; Close ends the
/// session; and a host that died leaves a failed item, with Show off, until it is closed.

#include "ObservatoryWindowCase.h"

#include "Test_ShellCore/StandInExtensionSessions.h"

#include "tasks/TaskPresenter.h"

#include "components/AnIconRail.h"
#include "components/RailItem.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessPresentation.h"
#include "core/ApplicationCore.h"
#include "tasks/TaskModel.h"

#include <QAction>
#include <QMenu>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

using an::tests::observatory::ObservatoryWindowCase;
using an::tests::shellcore::StandInExtensionSessions;
using Window = an::ui::observatory::ObservatoryWindow;
using an::ui::ProcessRunState;
using an::ui::shell::Task;
using an::ui::shell::TaskId;

/// The sessions every case of the suite plays the host manager for; the window is started over them.
[[nodiscard]] StandInExtensionSessions& sessions()
{
    static StandInExtensionSessions standIn;
    return standIn;
}

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// The item the rail shows under an id; a default item when it shows none.
[[nodiscard]] an::ui::RailItem railItem(const an::ui::AnIconRail& rail, const QString& id)
{
    const std::vector<an::ui::RailItem>& items = rail.items();
    const auto found =
        std::find_if(items.begin(), items.end(), [&id](const an::ui::RailItem& item) { return item.id == id; });
    return found != items.end() ? *found : an::ui::RailItem{};
}

/// The texts of a menu's entries, a separator as "|".
[[nodiscard]] QStringList textsOf(const QMenu& menu)
{
    QStringList texts;
    for (const QAction* action : menu.actions())
    {
        texts << (action->isSeparator() ? QStringLiteral("|") : action->text());
    }
    return texts;
}

/// The entry of a menu by its text; null for none.
[[nodiscard]] QAction* entryOf(const QMenu& menu, const QString& text)
{
    for (QAction* action : menu.actions())
    {
        if (action->text() == text)
        {
            return action;
        }
    }
    return nullptr;
}

/// The Observatory window started over the stand-in's sessions.
class ExternalTasks : public ObservatoryWindowCase
{
protected:
    static void SetUpTestSuite()
    {
        s_running = an::tests::observatory::startObservatory(
            [](an::ui::shell::ApplicationCore& core) { core.setExtensionSessions(&sessions()); });
        s_platformActivatesWindows = window().isActiveWindow();
    }

    /// Every session the case left is ended, and every failed task it left closed.
    void TearDown() override
    {
        for (const StandInExtensionSessions::Standing& standing : sessions().sessionStandings())
        {
            sessions().end(standing.sessionId);
        }
        an::ui::shell::TaskModel& model = tasks().model();
        std::vector<TaskId> failed;
        for (const Task& task : model.tasks())
        {
            if (task.hostFailed)
            {
                failed.push_back(task.id);
            }
        }
        for (const TaskId id : failed)
        {
            (void)model.closeExternal(id);
        }
        ObservatoryWindowCase::TearDown();
    }

    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    /// A session of the extension host on `target`, its host ready.
    [[nodiscard]] static StandInExtensionSessions::Standing readySession(const std::string& sessionId,
                                                                         ImageViewControl* target = nullptr)
    {
        StandInExtensionSessions::Standing standing = StandInExtensionSessions::session(
            sessionId, "an.demo.external.process", QStringLiteral("Demo External"), target);
        standing.host = an::ui::extensions::ExtensionHostStanding::Ready;
        return standing;
    }

    /// The rail's id of a session's task; empty for a session the window shows no task of.
    [[nodiscard]] static QString railIdOf(const std::string& sessionId)
    {
        const Task* task = tasks().model().taskForSession(sessionId);
        return task != nullptr ? Window::railTaskId(task->id) : QString();
    }
};

} // namespace

// A session is a rail item from its start: its name and its image's, the image's thumbnail once it is drawn, the
// extension's glyph, and a tooltip that says it opens in its own window; a session on no image is its glyph alone. Its
// end takes the item away.
TEST_F(ExternalTasks, ASessionIsARailItemThatOpensInItsOwnWindow)
{
    ImageViewControl* const view = openView(QStringLiteral("external-item"));
    ASSERT_NE(view, nullptr);
    sessions().stand(readySession("item", view));
    settle();
    const QString id = railIdOf("item");
    ASSERT_FALSE(id.isEmpty());
    an::ui::RailItem item = railItem(*window().rail(), id);
    EXPECT_EQ(item.section, an::ui::RailSection::Task);
    EXPECT_TRUE(item.external);
    EXPECT_FALSE(item.floating);
    EXPECT_EQ(item.text, QStringLiteral("Demo External - %1").arg(QString::fromStdString(view->getUniqueName())));
    EXPECT_EQ(item.glyphKey, QString::fromLatin1(an::ui::shell::TaskModel::kGenericGlyph));
    EXPECT_TRUE(window().rail()->itemToolTip(id).contains(QStringLiteral("opens in its own window")))
        << window().rail()->itemToolTip(id).toStdString();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
        return !railItem(*window().rail(), id).thumbnail.isNull();
    })) << "the image's thumbnail is drawn";

    sessions().stand(readySession("bare"));
    settle();
    const QString bare = railIdOf("bare");
    ASSERT_FALSE(bare.isEmpty());
    EXPECT_TRUE(railItem(*window().rail(), bare).thumbnail.isNull()) << "a session on no image is its glyph alone";
    EXPECT_EQ(railItem(*window().rail(), bare).text, QStringLiteral("Demo External"));

    sessions().end("item");
    settle();
    EXPECT_TRUE(railItem(*window().rail(), id).id.isEmpty()) << "the session's end takes its item";
    EXPECT_FALSE(railItem(*window().rail(), bare).id.isEmpty());
}

// The arc follows the run the host tells of, and the run blocks no image: the host holds no lock on it.
TEST_F(ExternalTasks, TheArcFollowsTheHostsRun)
{
    ImageViewControl* const view = openView(QStringLiteral("external-arc"));
    ASSERT_NE(view, nullptr);
    StandInExtensionSessions::Standing standing = readySession("arc", view);
    sessions().stand(standing);
    settle();
    const QString id = railIdOf("arc");
    ASSERT_FALSE(id.isEmpty());

    standing.run = an::ui::runningState({}, 0.42);
    sessions().stand(standing);
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::Running);
    EXPECT_EQ(railItem(*window().rail(), id).progress, std::optional<int>(42));
    EXPECT_TRUE(window().rail()->itemToolTip(id).contains(QStringLiteral("42 %")));
    EXPECT_FALSE(view->hasProcessingNotice()) << "a host's run blocks no image";

    standing.run = an::ui::runningState({}, 0.6);
    sessions().stand(standing);
    EXPECT_EQ(railItem(*window().rail(), id).progress, std::optional<int>(60));
    standing.run = ProcessRunState{.phase = ProcessRunState::Phase::Succeeded};
    sessions().stand(standing);
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::Succeeded);
    EXPECT_TRUE(railItem(*window().rail(), id).unseenResult);
}

// The menu has Show and Close, and nothing to float or attach: the window is the host's. Show asks only a host that
// raises its window, and a click on the item asks it too; Close ends the session.
TEST_F(ExternalTasks, TheMenuHasShowAndCloseAndAClickRaises)
{
    StandInExtensionSessions::Standing standing = readySession("menu");
    sessions().stand(standing);
    settle();
    const Task* task = tasks().model().taskForSession("menu");
    ASSERT_NE(task, nullptr);
    const TaskId taskId = task->id;
    {
        QMenu menu;
        window().buildTaskMenu(menu, taskId);
        EXPECT_EQ(textsOf(menu), (QStringList{QStringLiteral("Show"), QStringLiteral("|"), QStringLiteral("Close")}));
        EXPECT_FALSE(entryOf(menu, QStringLiteral("Show"))->isEnabled()) << "a host that does not raise its window";
        EXPECT_TRUE(entryOf(menu, QStringLiteral("Close"))->isEnabled());
    }

    standing.raisesItsWindow = true;
    sessions().stand(standing);
    {
        QMenu menu;
        window().buildTaskMenu(menu, taskId);
        ASSERT_TRUE(entryOf(menu, QStringLiteral("Show"))->isEnabled());
        entryOf(menu, QStringLiteral("Show"))->trigger();
    }
    EXPECT_EQ(sessions().raised(), (std::vector<std::string>{"menu"}));

    const QString id = Window::railTaskId(taskId);
    const QRect place = window().rail()->itemRect(id);
    ASSERT_TRUE(place.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
    settle();
    EXPECT_EQ(sessions().raised(), (std::vector<std::string>{"menu", "menu"})) << "a click raises the window";

    {
        QMenu menu;
        window().buildTaskMenu(menu, taskId);
        entryOf(menu, QStringLiteral("Close"))->trigger();
    }
    settle();
    EXPECT_EQ(sessions().closed().back(), "menu");
    EXPECT_EQ(tasks().model().task(taskId), nullptr);
    EXPECT_TRUE(railItem(*window().rail(), id).id.isEmpty());
}

// A host that died leaves its item failed, with why, until the person closes it; there is nothing to show.
TEST_F(ExternalTasks, AHostThatDiesLeavesAFailedItemUntilItIsDismissed)
{
    StandInExtensionSessions::Standing standing = readySession("died");
    standing.raisesItsWindow = true;
    sessions().stand(standing);
    settle();
    const Task* task = tasks().model().taskForSession("died");
    ASSERT_NE(task, nullptr);
    const TaskId taskId = task->id;
    const QString id = Window::railTaskId(taskId);

    sessions().end("died", true, QStringLiteral("The extension stopped responding."));
    settle();
    const an::ui::RailItem item = railItem(*window().rail(), id);
    ASSERT_FALSE(item.id.isEmpty()) << "the failed item stays";
    EXPECT_EQ(item.runPhase, an::ui::RailRunPhase::Failed);
    EXPECT_TRUE(item.unseenResult);
    EXPECT_TRUE(window().rail()->itemToolTip(id).contains(QStringLiteral("failed")));

    const QRect place = window().rail()->itemRect(id);
    ASSERT_TRUE(place.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
    settle();
    EXPECT_TRUE(sessions().raised().empty() || sessions().raised().back() != "died")
        << "a host that is gone shows nothing";
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::Failed) << "a click does not dismiss it";

    QMenu menu;
    window().buildTaskMenu(menu, taskId);
    EXPECT_FALSE(entryOf(menu, QStringLiteral("Show"))->isEnabled());
    ASSERT_TRUE(entryOf(menu, QStringLiteral("Close"))->isEnabled());
    entryOf(menu, QStringLiteral("Close"))->trigger();
    settle();
    EXPECT_EQ(tasks().model().task(taskId), nullptr);
    EXPECT_TRUE(railItem(*window().rail(), id).id.isEmpty());
}
