// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The views the application holds and what it tells the shell of them: each view under a name of its own; the active
/// view, and the views an action acts on and looks through; a view gone from the list before the shell is told, and the
/// shell sent home when the last goes; what a worker asks done on the GUI thread; and who asked for a new view - the
/// person, whose views the shell shows, or a process, where the shell keeps its place.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "views/ViewService.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"

#include <gtest/gtest.h>

#include <QPointer>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::documentNamed;
using an::tests::shellcore::pumpUntil;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::shell::ApplicationCore;
using an::ui::shell::ViewService;

class ViewServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_widgetsBefore = an::tests::shellcore::widgetsNow();
        ASSERT_TRUE(m_root.isValid());
        m_session = an::tests::shellcore::openSession();
        ASSERT_NE(m_session, nullptr);
        m_core.openStores();
        m_core.setShell(&m_shell);
        m_core.setEngineSessionProvider(m_session.get());
        m_target = std::make_unique<an::ui::shell::ServiceEventTarget>(
            m_core, an::ui::shell::ServiceEventTarget::Dependencies{});
        m_core.setEventTarget(m_target.get());
        m_context = std::make_unique<an::ui::shell::ApplicationContext>(m_core, m_shell);
        an::ui::clearAppContext();
        an::ui::setAppContext(m_context.get());
    }

    void TearDown() override
    {
        // A standard view is made in a dock widget that holds it; the case deletes the docks it made, and so their
        // views.
        for (const QPointer<QWidget>& dock : m_docks)
        {
            delete dock.data();
        }
        for (const QPointer<ImageViewControl>& view : m_views)
        {
            delete view.data();
        }
        an::tests::shellcore::settle();
        m_core.discardPendingEvents();
        m_core.setEventTarget(nullptr);
        an::ui::clearAppContext();
        m_core.setEngineSessionProvider(nullptr);
        m_core.setShell(nullptr);
        m_core.closeStores();
        const QStringList left = an::tests::shellcore::widgetsLeftSince(m_widgetsBefore);
        EXPECT_TRUE(left.isEmpty()) << "the case left widgets behind: "
                                    << left.join(QStringLiteral(", ")).toStdString();
    }

    /// A view a process makes of a new document named `name`, beside `neighbour` when given; the case deletes it.
    ImageViewControl* processView(const QString& name, ImageViewControl* neighbour = nullptr)
    {
        std::optional<an::ui::ImageDocument> document = documentNamed(m_session->engineSession(), name);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        an::ui::IViewFactory& factory = m_core.viewService();
        return kept(factory.createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, "made",
                                                               ViewControlMode::standard, neighbour, false, false));
    }

    /// A view the person opens of a new document named `name`; the case deletes it.
    ImageViewControl* userView(const QString& name)
    {
        std::optional<an::ui::ImageDocument> document = documentNamed(m_session->engineSession(), name);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        return kept(m_core.viewService().userViews().createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "opened", ViewControlMode::standard, nullptr, false, false));
    }

    ImageViewControl* kept(ImageViewControl* view)
    {
        EXPECT_NE(view, nullptr);
        m_views.emplace_back(view);
        if (view != nullptr)
        {
            m_docks.emplace_back(view->getDockWidget());
        }
        return view;
    }

    [[nodiscard]] std::vector<std::string> namesOfListedViews()
    {
        std::vector<std::string> names;
        for (ImageViewControl* view : m_core.viewService().getListOfViewControls())
        {
            names.push_back(view->getUniqueName());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    std::unique_ptr<SessionProvider> m_session;
    RecordingShell m_shell;
    ApplicationCore m_core;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
    QSet<QWidget*> m_widgetsBefore;
};

TEST_F(ViewServiceTest, EachViewHasANameOfItsOwn)
{
    ImageViewControl* first = processView(QStringLiteral("m31"));
    ImageViewControl* second = processView(QStringLiteral("m31"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(first->getUniqueName(), "m31");
    EXPECT_NE(second->getUniqueName(), first->getUniqueName());
    EXPECT_EQ(m_core.viewService().getViewControlByName(second->getUniqueName()), second);
    const std::string next = m_core.viewService().createViewControlUniqueName("m31");
    EXPECT_NE(next, first->getUniqueName());
    EXPECT_NE(next, second->getUniqueName());
    EXPECT_EQ(m_core.viewService().createViewControlUniqueName("m42"), "m42");
}

TEST_F(ViewServiceTest, TheActiveViewIsTheOneActivatedLastAndTheShellShowsIt)
{
    ImageViewControl* first = processView(QStringLiteral("first"));
    ImageViewControl* second = processView(QStringLiteral("second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    m_shell.calls.clear();

    m_core.viewService().setActiveImageViewControl(first);

    EXPECT_EQ(m_core.viewService().getActiveImageViewControl(), first);
    const std::vector<std::string> told = m_shell.callsAbout("first");
    ASSERT_GE(told.size(), 2u);
    EXPECT_EQ(told[0], "activate first");
    EXPECT_EQ(told[1], "info first");
    EXPECT_TRUE(m_shell.callsAbout("second").empty());
}

TEST_F(ViewServiceTest, AViewLeavesTheListBeforeTheShellIsToldAndTheLastSendsTheShellHome)
{
    ImageViewControl* first = processView(QStringLiteral("first"));
    ImageViewControl* second = processView(QStringLiteral("second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    m_shell.calls.clear();

    // What the list holds when the shell is told a view went: the view is still there to be asked, and no longer
    // listed.
    std::vector<std::vector<std::string>> listedWhenTold;
    m_shell.onViewRemoved = [this, &listedWhenTold](ImageViewControl& /*view*/) {
        listedWhenTold.push_back(namesOfListedViews());
    };

    m_core.viewService().removeViewControl(first, true);
    EXPECT_EQ(namesOfListedViews(), (std::vector<std::string>{"second"}));
    EXPECT_TRUE(m_shell.asked("removed first"));
    EXPECT_FALSE(m_shell.asked("home")) << "a view is still open";

    m_core.viewService().removeViewControl(second, true);
    EXPECT_TRUE(namesOfListedViews().empty());
    const auto removed = std::find(m_shell.calls.cbegin(), m_shell.calls.cend(), std::string("removed second"));
    const auto home = std::find(m_shell.calls.cbegin(), m_shell.calls.cend(), std::string("home"));
    ASSERT_NE(removed, m_shell.calls.cend());
    ASSERT_NE(home, m_shell.calls.cend());
    EXPECT_LT(removed, home) << "the shell went home before it was told the last view went";
    EXPECT_EQ(listedWhenTold,
              (std::vector<std::vector<std::string>>{std::vector<std::string>{"second"}, std::vector<std::string>{}}));
    m_shell.onViewRemoved = nullptr;
}

TEST_F(ViewServiceTest, WhatAWorkerAsksIsDoneOnTheGuiThread)
{
    ImageViewControl* first = processView(QStringLiteral("first"));
    ImageViewControl* second = processView(QStringLiteral("second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    m_core.viewService().setActiveImageViewControl(first);
    m_shell.calls.clear();
    m_shell.threads.clear();

    std::atomic_bool activated{false};
    std::thread worker([this, second, &activated]() { m_core.setActiveImageViewControlAsync(second, &activated); });
    worker.join();
    EXPECT_EQ(m_core.viewService().getActiveImageViewControl(), first) << "the worker did the GUI thread's work";

    ASSERT_TRUE(pumpUntil([&activated]() { return activated.load(); }));
    EXPECT_EQ(m_core.viewService().getActiveImageViewControl(), second);
    ASSERT_FALSE(m_shell.threads.empty());
    EXPECT_TRUE(std::all_of(m_shell.threads.cbegin(), m_shell.threads.cend(),
                            [](const std::thread::id thread) { return thread == std::this_thread::get_id(); }));
}

TEST_F(ViewServiceTest, AViewThePersonOpensIsTheirsAndTheShellShowsTheViews)
{
    ImageViewControl* view = userView(QStringLiteral("opened"));
    ASSERT_NE(view, nullptr);

    const std::vector<std::string> told = m_shell.callsAbout("opened");
    ASSERT_FALSE(told.empty());
    EXPECT_EQ(told.front(), "added opened user");
    const auto added = std::find(m_shell.calls.cbegin(), m_shell.calls.cend(), std::string("added opened user"));
    ASSERT_NE(added, m_shell.calls.cend());
    ASSERT_NE(added + 1, m_shell.calls.cend());
    EXPECT_EQ(*(added + 1), "showViews") << "the views are shown right after the shell is told of the view";
    EXPECT_TRUE(m_shell.asked("place opened beside none"));
}

TEST_F(ViewServiceTest, AViewAProcessMakesIsTheProcesssAndTheShellKeepsItsPlace)
{
    ImageViewControl* view = processView(QStringLiteral("result"));
    ASSERT_NE(view, nullptr);

    EXPECT_TRUE(m_shell.asked("added result process"));
    EXPECT_TRUE(m_shell.asked("place result beside none"));
    EXPECT_FALSE(m_shell.asked("showViews")) << "a process's view moved the shell to its views";
}

TEST_F(ViewServiceTest, ANewViewIsPlacedBesideTheNeighbourItWasMadeFrom)
{
    ImageViewControl* source = processView(QStringLiteral("source"));
    ASSERT_NE(source, nullptr);
    ImageViewControl* derived = processView(QStringLiteral("derived"), source);
    ASSERT_NE(derived, nullptr);

    EXPECT_TRUE(m_shell.asked("place derived beside source"));
}

TEST_F(ViewServiceTest, TheSubjectAndTheLookingViewAreTheActiveViewUnlessATaskSetsThem)
{
    ImageViewControl* first = processView(QStringLiteral("first"));
    ImageViewControl* target = processView(QStringLiteral("target"));
    ImageViewControl* canvas = processView(QStringLiteral("canvas"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(canvas, nullptr);
    ViewService& views = m_core.viewService();

    views.setActiveImageViewControl(first);
    EXPECT_EQ(views.subjectView(), first);
    EXPECT_EQ(views.lookingView(), first);

    views.setTaskSubject(target, canvas);
    EXPECT_EQ(views.subjectView(), target);
    EXPECT_EQ(views.lookingView(), canvas);
    views.setActiveImageViewControl(first);
    EXPECT_EQ(views.subjectView(), target) << "the task's target stays the subject while the task is shown";
    EXPECT_EQ(views.lookingView(), canvas);

    views.clearTaskSubject();
    EXPECT_EQ(views.subjectView(), first);
    EXPECT_EQ(views.lookingView(), first);

    // A task subject that goes leaves the active view the subject again.
    views.setTaskSubject(target, canvas);
    views.removeViewControl(target, true);
    EXPECT_EQ(views.subjectView(), views.getActiveImageViewControl());
}

} // namespace
