// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The binder of a floating toolbar and a history strip, over views of a real session: it shows a view's actions, name
/// and zoom in the toolbar and its history in the strip; told another view it shows that one and hears nothing more of
/// the first; the toolbar and the strip may show two views; two binders never share a toolbar; a view that goes is
/// shown no more; a step clicked moves the image there; More opens the view's menu as the host fills it.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "views/ViewChromeBinder.h"
#include "views/ViewService.h"

#include "components/AnFloatingToolbar.h"
#include "components/AnHistoryStrip.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QAbstractButton>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::pumpUntil;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::AnFloatingToolbar;
using an::ui::AnHistoryStrip;
using an::ui::shell::ApplicationCore;

class ViewChromeBinderFixture : public ::testing::Test
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
        m_binders.clear();
        m_toolbars.clear();
        m_strips.clear();
        m_core.viewService().manager().clear();
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

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_session->engineSession();
    }

    /// A view of an image of one value named `name`, made by the views' factory; the case deletes it.
    ImageViewControl* viewNamed(const QString& name)
    {
        std::optional<an::ui::ImageDocument> document = an::tests::shellcore::documentNamed(session(), name);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        an::ui::IViewFactory& factory = m_core.viewService();
        ImageViewControl* view = factory.createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "made", ViewControlMode::standard, nullptr, false, false);
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            view->setChromeMode(an::ui::ViewChromeMode::External);
            m_views.emplace_back(view);
            m_docks.emplace_back(view->getDockWidget());
            EXPECT_TRUE(pumpUntil([view]() { return view->isWorkIdle(); }));
        }
        return view;
    }

    /// Edits a view's image into one of a value, recorded in its history as `name`; waits until it is shown.
    void edit(ImageViewControl& view, const std::string& name)
    {
        std::optional<an::ui::ImageDocument> next = an::tests::shellcore::documentNamed(session(), QStringLiteral("x"));
        ASSERT_TRUE(next.has_value());
        an::Result<an::consumer::ImageAsset> copy = next->asset()->snapshot();
        ASSERT_TRUE(copy.hasValue()) << copy.status().message();
        auto held = std::make_shared<an::consumer::ImageAsset>(std::move(*copy));
        std::optional<bool> shown;
        view.editImage(
            name, "Edited by the case",
            [held](const an::consumer::EngineSession&, const an::consumer::AssetWriteLease&, std::stop_token) {
                return an::Result<an::consumer::ImageAsset>(std::move(*held));
            },
            [&shown](const bool done) { shown = done; });
        ASSERT_TRUE(pumpUntil([&shown, &view]() { return shown.has_value() && view.isWorkIdle(); }));
        ASSERT_TRUE(*shown) << "the edit " << name << " was not shown";
    }

    /// A toolbar, a strip and their binder, kept by the case.
    struct Chrome
    {
        AnFloatingToolbar* toolbar = nullptr;
        AnHistoryStrip* strip = nullptr;
        an::ui::shell::ViewChromeBinder* binder = nullptr;
    };

    Chrome makeChrome()
    {
        m_toolbars.push_back(std::make_unique<AnFloatingToolbar>());
        m_strips.push_back(std::make_unique<AnHistoryStrip>());
        m_strips.back()->resize(1600, m_strips.back()->sizeHint().height());
        m_binders.push_back(
            std::make_unique<an::ui::shell::ViewChromeBinder>(m_toolbars.back().get(), m_strips.back().get()));
        return Chrome{m_toolbars.back().get(), m_strips.back().get(), m_binders.back().get()};
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    std::unique_ptr<SessionProvider> m_session;
    ApplicationCore m_core;
    RecordingShell m_shell;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    std::vector<std::unique_ptr<an::ui::shell::ViewChromeBinder>> m_binders;
    std::vector<std::unique_ptr<AnFloatingToolbar>> m_toolbars;
    std::vector<std::unique_ptr<AnHistoryStrip>> m_strips;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
    QSet<QWidget*> m_widgetsBefore;
};

class ViewChromeBinder : public ViewChromeBinderFixture
{
};

} // namespace

// Bound to a view, the toolbar shows the view's own actions and choices, its name and its zoom, and the strip its
// history's steps and its Undo, Redo and Image History; an edit of the view reaches the strip.
TEST_F(ViewChromeBinder, ShowsAViewsActionsAndHistory)
{
    ImageViewControl* view = viewNamed(QStringLiteral("M42"));
    ASSERT_NE(view, nullptr);
    const Chrome chrome = makeChrome();
    chrome.binder->bind(view);
    EXPECT_EQ(chrome.binder->toolbarView(), view);
    EXPECT_EQ(chrome.binder->historyView(), view);
    EXPECT_EQ(chrome.toolbar->actions(), view->chromeActions());
    EXPECT_EQ(chrome.toolbar->zoomText(), QStringLiteral("%1 %").arg(view->getCurrentZoom(), 0, 'f', 2));
    ASSERT_NE(view->viewHistory(), nullptr);
    EXPECT_EQ(chrome.strip->steps(), view->viewHistory()->steps());
    EXPECT_EQ(chrome.strip->undoButton()->isEnabled(), view->viewHistory()->hasUndo());

    edit(*view, "Curves Transformation");
    EXPECT_EQ(chrome.strip->steps(), view->viewHistory()->steps()) << "the strip follows the history";
    EXPECT_TRUE(chrome.strip->shownChips().contains(QStringLiteral("Curves Transformation")));
    EXPECT_TRUE(chrome.strip->undoButton()->isEnabled()) << "Undo as the history has it";

    view->zoomIn();
    ASSERT_TRUE(pumpUntil([view]() { return view->isWorkIdle(); }));
    EXPECT_EQ(chrome.toolbar->zoomText(), QStringLiteral("%1 %").arg(view->getCurrentZoom(), 0, 'f', 2))
        << "the zoom follows the view's";
    chrome.binder->setViewCount(2);
    EXPECT_TRUE(chrome.toolbar->shownParts().contains(QStringLiteral("name")));
}

// Told another view, the binder shows that one and hears nothing more of the first.
TEST_F(ViewChromeBinder, RebindsWhenToldAnotherView)
{
    ImageViewControl* first = viewNamed(QStringLiteral("first"));
    ImageViewControl* second = viewNamed(QStringLiteral("second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const Chrome chrome = makeChrome();
    chrome.binder->setViewCount(2);
    chrome.binder->bind(first);
    ASSERT_NE(chrome.toolbar->buttonFor(first->chromeActions().fit), nullptr);

    chrome.binder->bind(second);
    EXPECT_EQ(chrome.binder->toolbarView(), second);
    EXPECT_EQ(chrome.toolbar->actions(), second->chromeActions());
    EXPECT_EQ(chrome.toolbar->buttonFor(first->chromeActions().fit), nullptr);
    EXPECT_NE(chrome.toolbar->buttonFor(second->chromeActions().fit), nullptr);
    EXPECT_EQ(chrome.strip->steps(), second->viewHistory()->steps());

    const an::ui::ViewHistorySteps shown = chrome.strip->steps();
    edit(*first, "Only on the first");
    EXPECT_EQ(chrome.strip->steps(), shown) << "the first view's history is heard no more";
    edit(*second, "On the second");
    EXPECT_EQ(chrome.strip->steps(), second->viewHistory()->steps());

    // A click on one of the second view's steps moves its image there.
    QAbstractButton* chip = chrome.strip->chipForStep(0u);
    ASSERT_NE(chip, nullptr);
    chip->click();
    ASSERT_TRUE(pumpUntil([second]() { return second->viewHistory()->steps().current == 0u && second->isWorkIdle(); }));
    EXPECT_EQ(chrome.strip->steps().current, 0u);
}

// The toolbar may show one view and the strip another's history: a task page's canvas and its target.
TEST_F(ViewChromeBinder, ShowsTheToolbarAndTheHistoryOfTwoViews)
{
    ImageViewControl* canvas = viewNamed(QStringLiteral("canvas"));
    ImageViewControl* target = viewNamed(QStringLiteral("target"));
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(target, nullptr);
    const Chrome chrome = makeChrome();
    chrome.binder->bindToolbar(canvas);
    chrome.binder->bindHistory(target);
    EXPECT_EQ(chrome.toolbar->actions(), canvas->chromeActions());
    EXPECT_EQ(chrome.strip->steps(), target->viewHistory()->steps());
    EXPECT_EQ(chrome.strip->undoButton()->toolTip(), target->chromeActions().undo->toolTip());
    edit(*target, "On the target");
    EXPECT_TRUE(chrome.strip->shownChips().contains(QStringLiteral("On the target")));
}

// Two binders each keep their own toolbar and strip: showing the same view they show it twice, each in its own buttons,
// and one told another view leaves the other as it was.
TEST_F(ViewChromeBinder, TwoBindersDoNotShareAToolbar)
{
    ImageViewControl* first = viewNamed(QStringLiteral("first"));
    ImageViewControl* second = viewNamed(QStringLiteral("second"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const Chrome workspace = makeChrome();
    const Chrome task = makeChrome();
    workspace.binder->bind(first);
    task.binder->bind(first);
    QAbstractButton* inWorkspace = workspace.toolbar->buttonFor(first->chromeActions().zoomIn);
    QAbstractButton* inTask = task.toolbar->buttonFor(first->chromeActions().zoomIn);
    ASSERT_NE(inWorkspace, nullptr);
    ASSERT_NE(inTask, nullptr);
    EXPECT_NE(inWorkspace, inTask) << "each toolbar has its own buttons for the view's one action";
    EXPECT_EQ(inWorkspace->parent(), workspace.toolbar);
    EXPECT_EQ(inTask->parent(), task.toolbar);

    task.binder->bind(second);
    EXPECT_EQ(workspace.toolbar->actions(), first->chromeActions()) << "the other binder's toolbar is as it was";
    EXPECT_EQ(workspace.strip->steps(), first->viewHistory()->steps());
    EXPECT_EQ(task.toolbar->actions(), second->chromeActions());
    edit(*first, "On the first");
    EXPECT_TRUE(workspace.strip->shownChips().contains(QStringLiteral("On the first")));
    EXPECT_FALSE(task.strip->shownChips().contains(QStringLiteral("On the first")));
}

// A view that goes is shown no more: the toolbar and the strip are emptied, and nothing reaches the view's history.
TEST_F(ViewChromeBinder, AViewThatGoesIsShownNoMore)
{
    ImageViewControl* view = viewNamed(QStringLiteral("going"));
    ASSERT_NE(view, nullptr);
    const Chrome chrome = makeChrome();
    chrome.binder->bind(view);
    ASSERT_FALSE(chrome.strip->shownChips().isEmpty());

    QPointer<QWidget> dock = view->getDockWidget();
    m_core.viewService().manager().clear();
    delete view;
    delete dock.data();
    an::tests::shellcore::settle();
    EXPECT_EQ(chrome.binder->toolbarView(), nullptr);
    EXPECT_EQ(chrome.binder->historyView(), nullptr);
    EXPECT_EQ(chrome.toolbar->actions(), an::ui::ViewChromeActions{});
    EXPECT_TRUE(chrome.toolbar->shownParts().isEmpty());
    EXPECT_TRUE(chrome.strip->shownChips().isEmpty());
    EXPECT_FALSE(chrome.strip->undoButton()->isVisibleTo(chrome.strip));
    chrome.binder->bind(nullptr);
}

// More opens the view's menu as the host fills it, over the toolbar; with no filler it opens nothing.
TEST_F(ViewChromeBinder, MoreOpensTheViewsMenu)
{
    ImageViewControl* view = viewNamed(QStringLiteral("menu"));
    ASSERT_NE(view, nullptr);
    const Chrome chrome = makeChrome();
    chrome.binder->bind(view);
    chrome.toolbar->moreButton()->click();
    EXPECT_EQ(chrome.binder->openMenu(), nullptr) << "with no filler, More opens nothing";

    ImageViewControl* filledFor = nullptr;
    chrome.binder->setMenuFiller([&filledFor](QMenu& menu, ImageViewControl& shown) {
        filledFor = &shown;
        menu.addAction(QStringLiteral("Duplicate"));
    });
    chrome.toolbar->moreButton()->click();
    EXPECT_EQ(filledFor, view);
    QMenu* menu = chrome.binder->openMenu();
    ASSERT_NE(menu, nullptr);
    ASSERT_EQ(menu->actions().size(), 1);
    EXPECT_EQ(menu->actions().front()->text(), QStringLiteral("Duplicate"));
    menu->close();
    an::tests::shellcore::settle();
    EXPECT_EQ(chrome.binder->openMenu(), nullptr) << "a closed menu goes";
}
