// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application context over the core and a shell: each service answered by the part of the application that
/// holds it, the shell's window as the default parent, a new view placed by the shell beside the neighbour the caller
/// gave, the views' maximizing and the status line reaching the shell, the shell sent home when the last view goes,
/// and what the window still answers reaching it through its dependencies.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "services/DialogService.h"
#include "services/FileDialogService.h"
#include "services/RecentFilesModel.h"
#include "views/ViewService.h"

#include "app/RuntimeStorageCoordinator.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::documentNamed;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::shell::ApplicationContext;
using an::ui::shell::ApplicationCore;

class ApplicationContextTest : public ::testing::Test
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
        ApplicationContext::Dependencies dependencies;
        dependencies.applyTheme = [this](const QString& themeId) { asked.push_back("theme " + themeId.toStdString()); };
        dependencies.openRecentSingleFile = [this](const QString& file) {
            asked.push_back("recent " + file.toStdString());
        };
        dependencies.openDroppedFiles = [this](const QStringList& files) {
            asked.push_back("dropped " + files.join(QLatin1Char(',')).toStdString());
        };
        m_context = std::make_unique<ApplicationContext>(m_core, m_shell, std::move(dependencies));
        an::ui::clearAppContext();
        an::ui::setAppContext(m_context.get());
    }

    void TearDown() override
    {
        // A standard view is made in a dock widget that holds it; the case deletes the docks it made, and so their
        // views.
        // The views leave the list first, as the window's going takes them out of it: a view deleted while it is
        // listed would leave the list naming it for the work still queued to read.
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

    /// A view made through the context's factory of a new document named `name`, beside `neighbour` when given.
    ImageViewControl* viewNamed(const QString& name, ImageViewControl* neighbour = nullptr)
    {
        std::optional<an::ui::ImageDocument> document = documentNamed(m_session->engineSession(), name);
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return nullptr;
        }
        ImageViewControl* view = m_context->getViewFactory()->createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "made", ViewControlMode::standard, neighbour, false, false);
        EXPECT_NE(view, nullptr);
        m_views.emplace_back(view);
        if (view != nullptr)
        {
            m_docks.emplace_back(view->getDockWidget());
        }
        return view;
    }

    std::vector<std::string> asked;
    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    std::unique_ptr<SessionProvider> m_session;
    RecordingShell m_shell;
    ApplicationCore m_core;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<ApplicationContext> m_context;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
    QSet<QWidget*> m_widgetsBefore;
};

TEST_F(ApplicationContextTest, EachServiceIsAnsweredByThePartThatHoldsIt)
{
    an::ui::IAppContext& context = *m_context;
    an::ui::shell::ViewService& views = m_core.viewService();

    EXPECT_EQ(context.getDialogService(), static_cast<an::ui::IDialogService*>(&m_core.dialogService()));
    EXPECT_EQ(context.getFileDialogService(), static_cast<an::ui::IFileDialogService*>(&m_core.fileDialogService()));
    EXPECT_EQ(context.getViewFactory(), static_cast<an::ui::IViewFactory*>(&views));
    EXPECT_EQ(context.getViewRegistry(), static_cast<an::ui::IViewRegistry*>(&views));
    EXPECT_EQ(context.getViewControlManager(), static_cast<an::ui::IViewControlManager*>(&views));
    EXPECT_EQ(context.getEngineSessionProvider(), m_session.get());
    EXPECT_EQ(context.getProcessPresenter(), m_shell.processPresenter());

    // The settings are the core's store: what one says, the other reads.
    ASSERT_NE(context.getGlobalSettings(), nullptr);
    ASSERT_TRUE(context.getGlobalSettings()->upsert(QStringLiteral("tests/answer"), QStringLiteral("42")));
    EXPECT_EQ(m_core.globalSettings()->select(QStringLiteral("tests/answer")), QStringLiteral("42"));

    // The temporary folder, the recent files and the folders remembered are the core's.
    ASSERT_NE(context.getTempFileService(), nullptr);
    EXPECT_EQ(context.getTempFileService()->getTempDirPath(), m_core.runtimeStorage().getTempDirPath());
    ASSERT_NE(context.getRecentFilesService(), nullptr);
    EXPECT_EQ(context.getRecentFilesService()->getRecentFilesList(), m_core.recentFiles().entries());
    ASSERT_NE(context.getPathMemoryService(), nullptr);
    ASSERT_TRUE(context.getPathMemoryService()->upsertPath(QStringLiteral("darks"), m_root.path()));
    EXPECT_EQ(context.getPathMemoryService()->selectPath(QStringLiteral("darks")), m_root.path());
}

TEST_F(ApplicationContextTest, TheDefaultParentIsTheShellsWindow)
{
    EXPECT_EQ(m_context->getMainWindow(), m_shell.mainWindow());
}

TEST_F(ApplicationContextTest, ANewViewIsPlacedByTheShellBesideTheNeighbourGiven)
{
    ImageViewControl* source = viewNamed(QStringLiteral("source"));
    ASSERT_NE(source, nullptr);
    ImageViewControl* derived = viewNamed(QStringLiteral("derived"), source);
    ASSERT_NE(derived, nullptr);

    EXPECT_TRUE(m_shell.asked("place source beside none"));
    EXPECT_TRUE(m_shell.asked("place derived beside source"));
    EXPECT_FALSE(m_shell.asked("showViews")) << "a view made through the context's factory is a process's";
}

TEST_F(ApplicationContextTest, MaximizingRestoringAndTheStatusLineReachTheShell)
{
    ImageViewControl* view = viewNamed(QStringLiteral("shown"));
    ASSERT_NE(view, nullptr);
    m_context->getViewRegistry()->setActiveImageViewControl(view);
    m_shell.calls.clear();

    m_context->getViewControlManager()->maximizeViewControl(view);
    m_context->getViewControlManager()->restoreViewControl(view);
    an::ui::IStatusBarService* const status = m_context->getStatusBarService();
    ASSERT_NE(status, nullptr);
    status->updateImageInfoInStatusBar();
    status->updateImageZoomInfoInStatusBar();
    status->updateImageCelestialCoordsInStatusBar(10.5, -20.25);
    status->clearImageCelestialCoordsInStatusBar();

    EXPECT_EQ(m_shell.calls, (std::vector<std::string>{"maximize shown", "restore shown", "info shown", "zoom shown",
                                                       "coordinates 10.500000 -20.250000", "coordinates cleared"}));
}

TEST_F(ApplicationContextTest, WithNoViewLeftTheShellIsSentHome)
{
    ImageViewControl* view = viewNamed(QStringLiteral("last"));
    ASSERT_NE(view, nullptr);
    m_shell.calls.clear();

    m_context->getViewControlManager()->removeViewControl(view, true);

    EXPECT_TRUE(m_shell.asked("removed last"));
    EXPECT_TRUE(m_shell.asked("home"));
}

TEST_F(ApplicationContextTest, WhatTheWindowStillAnswersReachesItThroughItsDependencies)
{
    m_context->getInterfaceSettingsRuntimeService()->applyTheme(QStringLiteral("dark"));
    m_context->getRecentFilesService()->openRecentSingleFile(QStringLiteral("m42.fits"));
    m_context->getRecentFilesService()->openDroppedFiles(
        QStringList{QStringLiteral("a.fits"), QStringLiteral("b.fits")});

    EXPECT_EQ(asked, (std::vector<std::string>{"theme dark", "recent m42.fits", "dropped a.fits,b.fits"}));
}

// The state of the actions is the core's coordinator's: an information action of the view the image actions act on
// follows what that view's own toolbar offers, and another view's changes nothing.
TEST_F(ApplicationContextTest, TheActionsStateIsTheCoresCoordinators)
{
    ImageViewControl* const other = viewNamed(QStringLiteral("other"));
    ImageViewControl* const active = viewNamed(QStringLiteral("active"));
    ASSERT_NE(other, nullptr);
    ASSERT_NE(active, nullptr);
    m_core.viewService().setActiveImageViewControl(active);
    QAction* const histogram = m_core.actions().action(an::ui::shell::ids::kHistogram);
    ASSERT_NE(histogram, nullptr);
    an::ui::IActionStateManager* const state = m_context->getActionStateManager();
    ASSERT_NE(state, nullptr);

    state->setCustomImageActionEnabled(active, GlobalViewControlActions::HISTOGRAM_ACTION, false);
    EXPECT_FALSE(histogram->isEnabled());
    state->setCustomImageActionEnabled(other, GlobalViewControlActions::HISTOGRAM_ACTION, true);
    EXPECT_FALSE(histogram->isEnabled()) << "a view the image actions do not act on changed the action";
    state->setCustomImageActionEnabled(active, GlobalViewControlActions::HISTOGRAM_ACTION, true);
    EXPECT_TRUE(histogram->isEnabled());
}

} // namespace
